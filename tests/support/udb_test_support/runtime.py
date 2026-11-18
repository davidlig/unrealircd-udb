"""Bounded, deterministic helpers for the isolated UDB runtime suite.

These helpers model only test I/O and process ownership. Database encoding and
digests are independent references; canonical UDB behavior remains under test.
"""

from __future__ import annotations

import hashlib
import ctypes
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
from ctypes import wintypes
from .native_reference import encode_component


class Wire:
    """Incremental IRC line reader that answers PING and rejects bad framing."""

    def __init__(self, sock: socket.socket, max_frame: int = 16384):
        if max_frame < 1:
            raise ValueError("maximum IRC frame size must be positive")
        self.sock = sock
        self.max_frame = max_frame
        self.buffer = bytearray()
        self.lines: list[str] = []

    def send(self, line: str) -> None:
        if "\r" in line or "\n" in line:
            raise ValueError("IRC command must contain one line without CR/LF")
        self.sock.sendall(line.encode("utf-8") + b"\r\n")

    def _accept_frames(self) -> None:
        while True:
            newline = self.buffer.find(b"\n")
            if newline < 0:
                if len(self.buffer) > self.max_frame:
                    raise ValueError(f"IRC frame exceeds {self.max_frame} bytes")
                return
            if newline > self.max_frame:
                raise ValueError(f"IRC frame exceeds {self.max_frame} bytes")
            frame = bytes(self.buffer[:newline])
            del self.buffer[: newline + 1]
            if frame.endswith(b"\r"):
                frame = frame[:-1]
            line = frame.decode("utf-8", errors="replace")
            self.lines.append(line)
            if line.startswith("PING "):
                self.send("PONG " + line[5:])

    def receive(self, timeout: float = 0.2) -> list[str]:
        """Read available complete lines, waiting at most ``timeout`` seconds."""
        if timeout < 0:
            raise ValueError("timeout must not be negative")
        start = len(self.lines)
        deadline = time.monotonic() + timeout
        while True:
            self._accept_frames()
            if len(self.lines) > start:
                return self.lines[start:]
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return self.lines[start:]
            self.sock.settimeout(remaining)
            try:
                data = self.sock.recv(min(4096, self.max_frame + 1))
            except socket.timeout:
                return self.lines[start:]
            except OSError as error:
                raise ConnectionError("IRC socket failed while receiving") from error
            if not data:
                if self.buffer:
                    raise ConnectionError("IRC peer closed with a truncated frame")
                raise ConnectionError("IRC peer closed the connection")
            self.buffer.extend(data)

    def wait(
        self,
        predicate,
        description: str,
        timeout: float = 5,
        start: int = 0,
    ) -> str:
        """Wait for a matching line, examining only lines at/after ``start``."""
        if not 0 <= start <= len(self.lines):
            raise ValueError("line start offset is outside the received line range")
        deadline = time.monotonic() + timeout
        while True:
            for line in self.lines[start:]:
                if predicate(line):
                    return line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for {description}; lines={self.lines[start:]}"
                )
            self.receive(remaining)

    def close(self) -> None:
        self.sock.close()


class OwnedProcess:
    """Own one foreground child and its log, including exceptional teardown."""

    def __init__(self, argv, log_path: Path, *, cwd: Path | None = None, env=None):
        self.argv = [os.fspath(arg) for arg in argv]
        self.log_path = Path(log_path)
        self.cwd = cwd
        self.env = None if env is None else dict(env)
        self.process: subprocess.Popen | None = None
        self.log_handle = None
        self.job = None

    def start(self) -> "OwnedProcess":
        if self.process is not None:
            raise RuntimeError("process owner may only be started once")
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_handle = self.log_path.open("w", encoding="utf-8")
        try:
            flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
            self.process = subprocess.Popen(
                self.argv,
                cwd=self.cwd,
                env=self.env,
                stdin=subprocess.DEVNULL,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
                start_new_session=(os.name != "nt"),
                creationflags=flags,
            )
            if os.name == "nt":
                kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
                kernel32.CreateJobObjectW.restype = ctypes.c_void_p
                kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                kernel32.OpenProcess.restype = wintypes.HANDLE
                kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
                kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
                kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
                kernel32.CloseHandle.restype = wintypes.BOOL
                self.job = kernel32.CreateJobObjectW(None, None)
                process_handle = kernel32.OpenProcess(0x0100 | 0x0001, False, self.process.pid)
                if not self.job or not process_handle:
                    if process_handle:
                        kernel32.CloseHandle(process_handle)
                    raise OSError(ctypes.get_last_error(), "could not create the test process job")
                try:
                    if not kernel32.AssignProcessToJobObject(self.job, process_handle):
                        raise OSError(ctypes.get_last_error(), "could not assign the test server to its job")
                finally:
                    kernel32.CloseHandle(process_handle)
        except BaseException:
            if self.process is not None and self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            if self.job:
                ctypes.WinDLL("kernel32", use_last_error=True).CloseHandle(self.job)
                self.job = None
            self.log_handle.close()
            self.log_handle = None
            raise
        return self

    def _posix_process_tree(root_pid: int) -> tuple[set[int], set[int]]:
        children_map: dict[int, list[int]] = {}
        pgid_map: dict[int, int] = {}
        proc_root = Path("/proc")
        if proc_root.is_dir():
            for entry in proc_root.iterdir():
                if not entry.name.isdigit():
                    continue
                pid = int(entry.name)
                try:
                    stat = (entry / "stat").read_text(encoding="utf-8", errors="replace")
                    rparen = stat.rfind(")")
                    if rparen < 0:
                        continue
                    fields = stat[rparen + 2:].split()
                    ppid = int(fields[1])
                    pgrp = int(fields[2])
                    children_map.setdefault(ppid, []).append(pid)
                    pgid_map[pid] = pgrp
                except (OSError, ValueError, IndexError):
                    continue
        pids = {root_pid}
        pgids = {root_pid}
        queue = [root_pid]
        while queue:
            current = queue.pop()
            if current in pgid_map and pgid_map[current] > 0:
                pgids.add(pgid_map[current])
            try:
                pgid = os.getpgid(current)
                if pgid > 0:
                    pgids.add(pgid)
            except OSError:
                pass
            for child in children_map.get(current, ()):
                if child not in pids:
                    pids.add(child)
                    pgids.add(child)
                    queue.append(child)
        return pids, pgids

    def stop(self, timeout: float = 5) -> None:
        process = self.process
        try:
            if process is not None and process.poll() is None:
                if os.name == "nt":
                    try:
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    except OSError:
                        pass
                    try:
                        process.wait(timeout=timeout)
                    except subprocess.TimeoutExpired:
                        if self.job:
                            ctypes.WinDLL("kernel32", use_last_error=True).TerminateJobObject(self.job, 1)
                        else:
                            process.kill()
                        process.wait()
                    return
                deadline = time.monotonic() + timeout
                pids, pgids = OwnedProcess._posix_process_tree(process.pid)
                for pid in sorted(pids - {process.pid}, reverse=True):
                    try:
                        os.kill(pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                for pgid in sorted(pgids, reverse=True):
                    try:
                        os.killpg(pgid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    for pid in sorted(pids, reverse=True):
                        try:
                            os.kill(pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    for pgid in sorted(pgids, reverse=True):
                        try:
                            os.killpg(pgid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    process.wait()
                while True:
                    alive = False
                    for pid in list(pids):
                        try:
                            os.kill(pid, 0)
                            alive = True
                        except ProcessLookupError:
                            pids.discard(pid)
                        except PermissionError:
                            alive = True
                    for pgid in list(pgids):
                        try:
                            os.killpg(pgid, 0)
                            alive = True
                        except ProcessLookupError:
                            pgids.discard(pgid)
                        except PermissionError:
                            alive = True
                    if not alive:
                        break
                    if time.monotonic() >= deadline:
                        for pid in list(pids):
                            try:
                                os.kill(pid, signal.SIGKILL)
                            except ProcessLookupError:
                                pids.discard(pid)
                        for pgid in list(pgids):
                            try:
                                os.killpg(pgid, signal.SIGKILL)
                            except ProcessLookupError:
                                pgids.discard(pgid)
                        deadline = time.monotonic() + timeout
                    time.sleep(0.02)
        finally:
            if self.job:
                ctypes.WinDLL("kernel32", use_last_error=True).CloseHandle(self.job)
                self.job = None
            if self.log_handle is not None:
                self.log_handle.close()
                self.log_handle = None

    def __enter__(self) -> "OwnedProcess":
        return self.start()

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.stop()
        return False
