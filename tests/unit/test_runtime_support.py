"""Runtime infrastructure failure paths are tested without a running IRCd."""

import os
import socket
import sys

import pytest

from udb_test_support.runtime import OwnedProcess, Wire, encode_component, tree_digest

pytestmark = pytest.mark.tooling


def test_reference_encoding_and_digest_are_independent_golden_vectors():
    assert encode_component("2001:db8::1") == "2001%3Adb8%3A%3A1"
    assert encode_component("a b%") == "a%20b%25"
    assert tree_digest({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    assert tree_digest({"user1::vhost": "user1.org"}) == "5819874dff9e29994db2eeb8b949bb63cf5c213303539d35b8a6bbaa05088049"


def test_wire_handles_fragmentation_ping_and_fresh_request_offsets():
    local, remote = socket.socketpair()
    try:
        wire = Wire(local)
        remote.sendall(b":server NOTICE a :part")
        wire.receive(0.1)
        assert not wire.lines
        remote.sendall(b"ial\r\nPING :token\r\n")
        wire.wait(lambda line: "partial" in line, "fragmented NOTICE", timeout=1)
        assert b"PONG :token\r\n" in remote.recv(128)
        start = len(wire.lines)
        with pytest.raises(TimeoutError):
            wire.wait(lambda line: "partial" in line, "must not reuse old NOTICE", start=start, timeout=0.01)
    finally:
        local.close()
        remote.close()


def test_wire_eof_is_failure_not_skip():
    local, remote = socket.socketpair()
    remote.close()
    try:
        with pytest.raises(ConnectionError):
            Wire(local).wait(lambda line: True, "missing reply", timeout=0.1)
    finally:
        local.close()


def test_wire_delivers_final_complete_frame_before_eof():
    local, remote = socket.socketpair()
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
    local, remote = socket.socketpair()
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
    with pytest.raises(ProcessLookupError):
        os.kill(pid, 0)


def test_owned_process_failed_launch_closes_log(tmp_path):
    owner = OwnedProcess(["/nonexistent/udb-test-command"], tmp_path / "failed.log")
    with pytest.raises(FileNotFoundError):
        owner.start()
    assert owner.log_handle is None
