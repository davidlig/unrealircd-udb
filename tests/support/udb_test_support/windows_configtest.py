"""Foreground Windows configtests with owned, bounded error-dialog handling.

UnrealIRCd 6.2.x uses ConfigErrorDLG / IDC_CONFIGERROR (1126) for rejected
configurations. Only that dialog in the child we launched is acknowledged;
crash dialogs and windows belonging to other processes remain untouched.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import math
from pathlib import Path
import subprocess
import time

from .runtime import OwnedProcess


class _Message(ctypes.Structure):
    # Include MSG's reserved DWORD explicitly, including on 32-bit Python.
    _fields_ = [
        ("hwnd", wintypes.HWND), ("message", wintypes.UINT),
        ("wParam", ctypes.c_size_t), ("lParam", ctypes.c_ssize_t),
        ("time", wintypes.DWORD), ("pt", wintypes.POINT),
        ("lPrivate", wintypes.DWORD),
    ]


class _Win32ConfigtestUI:
    """Process-scoped accessibility events, serviced on the calling thread."""

    def __init__(self, deadline: float):
        self.deadline = deadline
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.hook = self.process_handle = None
        self.error = None
        self.callbacks = []  # Keep native callbacks alive until UnhookWinEvent.
        self.event_type = ctypes.WINFUNCTYPE(
            None, wintypes.HANDLE, wintypes.DWORD, wintypes.HWND,
            wintypes.LONG, wintypes.LONG, wintypes.DWORD, wintypes.DWORD,
        )
        self.enum_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, ctypes.c_ssize_t)
        signatures = {
            "SetWinEventHook": (wintypes.HANDLE, [wintypes.DWORD, wintypes.DWORD, wintypes.HMODULE,
                self.event_type, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]),
            "UnhookWinEvent": (wintypes.BOOL, [wintypes.HANDLE]),
            "EnumWindows": (wintypes.BOOL, [self.enum_type, ctypes.c_ssize_t]),
            "GetWindowThreadProcessId": (wintypes.DWORD, [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]),
            "GetClassNameW": (ctypes.c_int, [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]),
            "GetWindowTextW": (ctypes.c_int, [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]),
            "GetDlgItem": (wintypes.HWND, [wintypes.HWND, ctypes.c_int]),
            "IsWindowVisible": (wintypes.BOOL, [wintypes.HWND]),
            "PostMessageW": (wintypes.BOOL, [wintypes.HWND, wintypes.UINT,
                ctypes.c_size_t, ctypes.c_ssize_t]),
            "SendMessageTimeoutW": (ctypes.c_ssize_t, [wintypes.HWND, wintypes.UINT,
                ctypes.c_size_t, ctypes.c_ssize_t, wintypes.UINT, wintypes.UINT,
                ctypes.POINTER(ctypes.c_size_t)]),
            "MsgWaitForMultipleObjectsEx": (wintypes.DWORD, [wintypes.DWORD,
                ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]),
            "PeekMessageW": (wintypes.BOOL, [ctypes.POINTER(_Message), wintypes.HWND,
                wintypes.UINT, wintypes.UINT, wintypes.UINT]),
            "TranslateMessage": (wintypes.BOOL, [ctypes.POINTER(_Message)]),
            "DispatchMessageW": (ctypes.c_ssize_t, [ctypes.POINTER(_Message)]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.user32, name)
            function.restype, function.argtypes = result, arguments
        self.kernel32.OpenProcess.restype = wintypes.HANDLE
        self.kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel32.CloseHandle.restype = wintypes.BOOL
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    def process_id(self, window) -> int:
        pid = wintypes.DWORD()
        self.user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
        return pid.value

    def class_name(self, window) -> str:
        text = ctypes.create_unicode_buffer(64)
        self.user32.GetClassNameW(window, text, len(text))
        return text.value

    def caption(self, window) -> str:
        text = ctypes.create_unicode_buffer(64)
        self.user32.GetWindowTextW(window, text, len(text))
        return text.value

    def control(self, window, identifier):
        return self.user32.GetDlgItem(window, identifier)

    def visible(self, window) -> bool:
        return bool(self.user32.IsWindowVisible(window))

    def _send(self, window, message, wparam=0, lparam=0) -> int:
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("configtest deadline expired while reading its dialog")
        result = ctypes.c_size_t()
        ctypes.set_last_error(0)
        if not self.user32.SendMessageTimeoutW(
            window, message, wparam, lparam, 0x23,  # BLOCK | ABORTIFHUNG | ERRORONEXIT
            min(1000, math.ceil(remaining * 1000)), ctypes.byref(result),
        ):
            raise OSError(ctypes.get_last_error(), "could not read the configtest error dialog")
        return result.value

    def read_text(self, window) -> str:
        # GetWindowText cannot read an Edit control in another process.
        length = self._send(window, 0x000E)  # WM_GETTEXTLENGTH
        if not 0 < length <= 65536:
            raise RuntimeError("empty or oversized configtest dialog diagnostic")
        text = ctypes.create_unicode_buffer(length + 1)
        copied = self._send(window, 0x000D, len(text), ctypes.addressof(text))  # WM_GETTEXT
        if copied != length:
            raise RuntimeError("incomplete configtest dialog diagnostic")
        return text.value

    def dismiss(self, window) -> None:
        if not self.user32.PostMessageW(window, 0x0111, 1, 0):  # WM_COMMAND / IDOK
            raise OSError(ctypes.get_last_error(), "could not acknowledge the configtest error dialog")

    def watch(self, pid: int, inspect) -> None:
        def visit(window):
            if self.error is None:
                try:
                    inspect(window)
                except BaseException as error:
                    self.error = error  # Never let exceptions disappear inside ctypes callbacks.

        def event(_hook, _event, window, object_id, child_id, _thread, _time):
            if object_id == 0 and child_id == 0:
                visit(window)

        event_callback = self.event_type(event)
        enum_callback = self.enum_type(lambda window, _data: (visit(window), True)[1])
        self.callbacks.extend((event_callback, enum_callback))
        self.process_handle = self.kernel32.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
        if not self.process_handle:
            raise OSError(ctypes.get_last_error(), "could not wait for the owned configtest process")
        message = _Message()
        self.user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 0)  # Create this thread's queue.
        self.hook = self.user32.SetWinEventHook(
            0x8002, 0x8002, None, event_callback, pid, 0, 0,  # OBJECT_SHOW / OUTOFCONTEXT
        )
        if not self.hook:
            raise OSError(ctypes.get_last_error(), "could not watch owned configtest dialogs")
        # Exactly one enumeration covers dialogs shown before the hook was installed.
        if not self.user32.EnumWindows(enum_callback, 0):
            raise OSError(ctypes.get_last_error(), "could not inspect existing configtest dialogs")
        if self.error is not None:
            raise self.error

    def wait(self, process, timeout: float) -> None:
        handles = (wintypes.HANDLE * 1)(self.process_handle)
        message = _Message()
        while True:
            if self.error is not None:
                raise self.error
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout)
            status = self.user32.MsgWaitForMultipleObjectsEx(
                1, handles, math.ceil(remaining * 1000), 0x04FF, 0x0004,  # ALLINPUT / INPUTAVAILABLE
            )
            if status == 0:  # The process exited; obtain its real status.
                process.wait(timeout=max(self.deadline - time.monotonic(), 0.001))
                return
            if status == 0x102:
                raise subprocess.TimeoutExpired(process.args, timeout)
            if status != 1:
                raise OSError(ctypes.get_last_error(), "configtest process/message wait failed")
            while self.user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
                if message.message == 0x0012:  # WM_QUIT is not a successful configtest exit.
                    raise RuntimeError("unexpected quit message during configtest")
                self.user32.TranslateMessage(ctypes.byref(message))
                self.user32.DispatchMessageW(ctypes.byref(message))
                if self.error is not None:
                    raise self.error
                if time.monotonic() >= self.deadline:
                    raise subprocess.TimeoutExpired(process.args, timeout)

    def close(self) -> None:
        try:
            if self.hook and not self.user32.UnhookWinEvent(self.hook):
                raise OSError(ctypes.get_last_error(), "could not release the configtest event hook")
        finally:
            self.hook = None
            if self.process_handle:
                handle = self.process_handle
                self.process_handle = None
                if not self.kernel32.CloseHandle(handle):
                    raise OSError(ctypes.get_last_error(), "could not release the configtest wait handle")


def _capture_config_error(ui, pid: int, window, seen: set, diagnostics: list[str]) -> None:
    if window in seen or ui.process_id(window) != pid or not ui.visible(window):
        return
    if ui.class_name(window) != "#32770" or ui.caption(window) != "Configuration Error":
        return
    text_control, button = ui.control(window, 1126), ui.control(window, 1)
    if not text_control or not button:
        return
    if (ui.process_id(text_control) != pid or ui.process_id(button) != pid or
            ui.class_name(text_control) != "Edit" or ui.class_name(button) != "Button"):
        return
    if len(seen) >= 16:
        raise RuntimeError("too many configtest error dialogs")
    seen.add(window)  # Guard against reentrant or duplicate accessibility events.
    diagnostic = ui.read_text(text_control)
    diagnostics.append(f"Configuration Error (pid {pid}):\n{diagnostic}")
    if ui.process_id(window) != pid:
        raise RuntimeError("configtest dialog ownership changed before acknowledgement")
    ui.dismiss(window)


def run_windows_configtest(argv, log_path: Path, *, cwd: Path, env=None, timeout: float = 30):
    """Return the actual configtest status, with GUI diagnostics in its log/output."""
    if timeout <= 0:
        raise ValueError("configtest timeout must be positive")
    deadline = time.monotonic() + timeout
    ui = _Win32ConfigtestUI(deadline)
    diagnostics, seen = [], set()
    owner = OwnedProcess(argv, log_path, cwd=cwd, env=env)
    try:
        with owner:
            try:
                ui.watch(owner.process.pid, lambda window: _capture_config_error(
                    ui, owner.process.pid, window, seen, diagnostics,
                ))
                ui.wait(owner.process, timeout)
            finally:
                ui.close()
    except BaseException as error:
        diagnostics.append(f"Configtest runner failed: {type(error).__name__}: {error}")
        raise
    finally:
        if diagnostics:
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with log_path.open("a", encoding="utf-8") as log:
                log.write("\n" + "\n".join(diagnostics) + "\n")
    output = log_path.read_text(encoding="utf-8", errors="replace")
    if seen and owner.process.returncode == 0:
        raise RuntimeError(f"configtest displayed an error but exited successfully:\n{output}")
    return subprocess.CompletedProcess(owner.argv, owner.process.returncode, stdout=output)
