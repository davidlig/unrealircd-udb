"""Runtime infrastructure failure paths are tested without a running IRCd."""

import os
import queue
import socket
import sys

import pytest

from udb_test_support.runtime import OwnedProcess, Wire, encode_component
from udb_test_support.native_reference import logical_digest

pytestmark = pytest.mark.tooling


class _MemorySocket:
    """Small duplex byte stream for Wire tests, without OS-specific socketpair."""

    def __init__(self):
        self.incoming = queue.Queue()
        self.peer = None
        self.timeout = None
        self.pending = bytearray()

    def sendall(self, data):
        self.peer.incoming.put(bytes(data))

    def settimeout(self, timeout):
        self.timeout = timeout

    def recv(self, size):
        if not self.pending:
            try:
                chunk = self.incoming.get(timeout=self.timeout)
            except queue.Empty as error:
                raise socket.timeout from error
            if chunk is None:
                return b""
            self.pending.extend(chunk)
        result = bytes(self.pending[:size])
        del self.pending[:size]
        return result

    def close(self):
        self.peer.incoming.put(None)


def _memory_socketpair():
    left, right = _MemorySocket(), _MemorySocket()
    left.peer, right.peer = right, left
    return left, right


def test_reference_encoding_and_digest_are_independent():
    assert encode_component("2001:db8::1") == "2001%3Adb8%3A%3A1"
    assert encode_component("a b%") == "a%20b%25"
    assert encode_component("*@host/24") == "*@host/24"
    assert logical_digest({}, 'N') != logical_digest({}, 'I')
    assert logical_digest({'Alice::vhost': 'a.test'}, 'N') == logical_digest({'alice::vhost': 'a.test'}, 'N')



def test_wire_handles_fragmentation_ping_and_fresh_request_offsets():
    local, remote = _memory_socketpair()
    try:
        wire = Wire(local)
        remote.sendall(b":server NOTICE a :part")
        wire.receive(0.1)
        assert not wire.lines
        remote.sendall(b"ial\r\nPING :token\r\n")
        wire.wait(lambda line: "partial" in line, "fragmented NOTICE", timeout=1)
        wire.wait(lambda line: line == "PING :token", "complete PING frame", timeout=1)
        assert b"PONG :token\r\n" in remote.recv(128)
        start = len(wire.lines)
        with pytest.raises(TimeoutError):
            wire.wait(lambda line: "partial" in line, "must not reuse old NOTICE", start=start, timeout=0.01)
    finally:
        local.close()
        remote.close()


def test_wire_eof_is_failure_not_skip():
    local, remote = _memory_socketpair()
    remote.close()
    try:
        with pytest.raises(ConnectionError):
            Wire(local).wait(lambda line: True, "missing reply", timeout=0.1)
    finally:
        local.close()


def test_wire_delivers_final_complete_frame_before_eof():
    local, remote = _memory_socketpair()
    remote.sendall(b"ERROR :UDB synchronization unavailable\r\n")
    remote.close()
    try:
        wire = Wire(local)
        assert wire.wait(lambda line: line.startswith("ERROR "), "final ERROR", timeout=0.1)
        with pytest.raises(ConnectionError):
            wire.receive(0.1)
    finally:
        local.close()


def test_wire_rejects_unbounded_frames():
    local, remote = _memory_socketpair()
    try:
        remote.sendall(b"x" * 100)
        with pytest.raises(ValueError):
            Wire(local, max_frame=32).receive(0.1)
    finally:
        local.close()
        remote.close()


def test_owned_process_cleans_up_on_exception(tmp_path):
    owner = OwnedProcess([sys.executable, "-c", "import time; time.sleep(60)"], tmp_path / "child.log")
    pid = None
    with pytest.raises(RuntimeError, match="test failure"):
        with owner:
            pid = owner.process.pid
            raise RuntimeError("test failure")
    assert owner.process.poll() is not None
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel32.GetExitCodeProcess.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        handle = kernel32.OpenProcess(0x1000, False, pid)
        assert handle
        try:
            exit_code = wintypes.DWORD()
            assert kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code))
            assert exit_code.value != 259  # STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    else:
        with pytest.raises(ProcessLookupError):
            os.kill(pid, 0)


def test_owned_process_failed_launch_closes_log(tmp_path):
    owner = OwnedProcess(["/nonexistent/udb-test-command"], tmp_path / "failed.log")
    with pytest.raises(FileNotFoundError):
        owner.start()
    assert owner.log_handle is None
