"""Configtest ownership and failure paths; native GUI proof runs on Windows."""

import ctypes
from ctypes import wintypes
import subprocess
import sys
import time
from types import SimpleNamespace

import pytest

from udb_test_support import node as node_module
from udb_test_support import windows_configtest as configtest


pytestmark = pytest.mark.tooling


class FakeUI:
    def __init__(self):
        self.windows = {
            10: {"pid": 123, "class": "#32770", "caption": "Configuration Error", "visible": True},
            11: {"pid": 123, "class": "Edit"},
            12: {"pid": 123, "class": "Button"},
        }
        self.controls = {1126: 11, 1: 12}
        self.dismissed = []
        self.reads = []
        self.text = "udb::propagator requires a valid server name"

    def process_id(self, window):
        return self.windows[window]["pid"]

    def class_name(self, window):
        return self.windows[window]["class"]

    def caption(self, window):
        return self.windows[window]["caption"]

    def visible(self, window):
        return self.windows[window]["visible"]

    def control(self, window, identifier):
        return self.controls.get(identifier)

    def read_text(self, window):
        self.reads.append(window)
        return self.text

    def dismiss(self, window):
        self.dismissed.append(window)


def test_owned_config_error_is_captured_before_acknowledgement_once():
    ui, seen, diagnostics = FakeUI(), set(), []

    def dismiss(window):
        assert diagnostics == [f"Configuration Error (pid 123):\n{ui.text}"]
        ui.dismissed.append(window)

    ui.dismiss = dismiss
    configtest._capture_config_error(ui, 123, 10, seen, diagnostics)
    configtest._capture_config_error(ui, 123, 10, seen, diagnostics)
    assert ui.dismissed == [10]
    assert ui.reads == [11]


@pytest.mark.parametrize("window,field,value", [
    (10, "pid", 456),
    (10, "visible", False),
    (10, "class", "OtherWindow"),
    (10, "caption", "Fatal Error"),
    (11, "pid", 456),
    (11, "class", "Static"),
    (12, "pid", 456),
    (12, "class", "OtherControl"),
])
def test_unowned_crash_or_unrecognized_windows_are_untouched(window, field, value):
    ui = FakeUI()
    ui.windows[window][field] = value
    diagnostics = []
    configtest._capture_config_error(ui, 123, 10, set(), diagnostics)
    assert not ui.dismissed and not ui.reads and not diagnostics


@pytest.mark.parametrize("identifier", [1126, 1])
def test_config_error_requires_both_expected_controls(identifier):
    ui = FakeUI()
    del ui.controls[identifier]
    configtest._capture_config_error(ui, 123, 10, set(), [])
    assert not ui.dismissed and not ui.reads


def test_failed_diagnostic_read_does_not_dismiss_dialog():
    ui = FakeUI()

    def read_text(window):
        raise OSError("read failed")

    ui.read_text = read_text
    with pytest.raises(OSError, match="read failed"):
        configtest._capture_config_error(ui, 123, 10, set(), [])
    assert not ui.dismissed


def test_ownership_is_rechecked_before_acknowledgement():
    ui = FakeUI()

    def read_text(window):
        ui.windows[10]["pid"] = 456
        return ui.text

    ui.read_text = read_text
    with pytest.raises(RuntimeError, match="ownership changed"):
        configtest._capture_config_error(ui, 123, 10, set(), [])
    assert not ui.dismissed


def test_error_dialog_count_is_bounded():
    with pytest.raises(RuntimeError, match="too many"):
        configtest._capture_config_error(FakeUI(), 123, 10, set(range(100, 116)), [])


@pytest.mark.parametrize("length", [0, 65537])
def test_native_text_read_rejects_empty_or_oversized_diagnostics(length):
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui._send = lambda *args: length
    with pytest.raises(RuntimeError, match="empty or oversized"):
        ui.read_text(11)


def test_native_text_read_uses_bounded_messages_and_preserves_unicode():
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    text = "Error de configuración: ñ / 東京"
    messages = []

    def send(window, message, wparam=0, lparam=0):
        messages.append(message)
        if message == 0x000E:
            return len(text)
        assert wparam == len(text) + 1
        buffer = ctypes.create_unicode_buffer(text)
        ctypes.memmove(lparam, buffer, ctypes.sizeof(buffer))
        return len(text)

    ui._send = send
    assert ui.read_text(11) == text
    assert messages == [0x000E, 0x000D]


def test_native_text_read_rejects_partial_diagnostic():
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui._send = lambda window, message, *args: 8 if message == 0x000E else 7
    with pytest.raises(RuntimeError, match="incomplete"):
        ui.read_text(11)


def test_event_callback_errors_propagate_instead_of_disappearing():
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.deadline = time.monotonic() + 1
    ui.callbacks, ui.error, ui.hook, ui.process_handle = [], None, None, None
    ui.event_type = ui.enum_type = lambda callback: callback
    events = []

    def hook(*args):
        assert args[4] == 123  # Native event subscription is process-scoped.
        events.append(args[3])
        return 5

    ui.kernel32 = SimpleNamespace(OpenProcess=lambda *args: 6)
    ui.user32 = SimpleNamespace(
        PeekMessageW=lambda *args: False,
        SetWinEventHook=hook,
        EnumWindows=lambda callback, data: True,
    )

    def inspect(window):
        raise OSError("callback failed")

    ui.watch(123, inspect)
    events[0](5, 0x8002, 10, -4, 0, 0, 0)
    assert ui.error is None  # Ignore non-window object events.
    events[0](5, 0x8002, 10, 0, 0, 0, 0)
    with pytest.raises(OSError, match="callback failed"):
        ui.wait(SimpleNamespace(args=["configtest"]), 30)


def test_watch_inspects_an_already_shown_dialog_once():
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.callbacks, ui.error, ui.hook, ui.process_handle = [], None, None, None
    ui.event_type = ui.enum_type = lambda callback: callback
    visits, subscriptions = [], []
    ui.kernel32 = SimpleNamespace(OpenProcess=lambda *args: 6)

    def hook(*args):
        subscriptions.append(args)
        return 5

    ui.user32 = SimpleNamespace(
        PeekMessageW=lambda *args: False,
        SetWinEventHook=hook,
        EnumWindows=lambda callback, data: callback(10, data),
    )
    ui.watch(123, visits.append)
    assert visits == [10]
    assert subscriptions[0][4:7] == (123, 0, 0)


@pytest.mark.parametrize("success", [True, False])
def test_native_messages_have_finite_deadlines_and_api_failures_propagate(monkeypatch, success):
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.deadline = time.monotonic() + 5
    calls = []

    def send(window, message, wparam, lparam, flags, milliseconds, result):
        calls.append((flags, milliseconds))
        ctypes.cast(result, ctypes.POINTER(ctypes.c_size_t))[0] = 8
        return success

    ui.user32 = SimpleNamespace(SendMessageTimeoutW=send)
    monkeypatch.setattr(ctypes, "get_last_error", lambda: 5, raising=False)
    monkeypatch.setattr(ctypes, "set_last_error", lambda code: None, raising=False)
    if success:
        assert ui._send(11, 0x000E) == 8
    else:
        with pytest.raises(OSError, match="could not read"):
            ui._send(11, 0x000E)
    assert calls[0][0] == 0x23
    assert 1 <= calls[0][1] <= 1000


@pytest.mark.parametrize("status", [0, 0x102, 0xFFFFFFFF])
def test_native_wait_distinguishes_exit_timeout_and_api_failure(status, monkeypatch):
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.deadline, ui.error, ui.process_handle = time.monotonic() + 1, None, 6
    ui.user32 = SimpleNamespace(MsgWaitForMultipleObjectsEx=lambda *args: status)
    monkeypatch.setattr(ctypes, "get_last_error", lambda: 5, raising=False)
    process = SimpleNamespace(args=["configtest"], wait=lambda **kwargs: 7)
    if status == 0:
        ui.wait(process, 30)
    else:
        error = subprocess.TimeoutExpired if status == 0x102 else OSError
        with pytest.raises(error):
            ui.wait(process, 30)


def test_native_wait_dispatches_events_before_observing_process_exit():
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.deadline, ui.error, ui.process_handle = time.monotonic() + 5, None, 6
    statuses, messages, observed = iter([1, 0]), iter([True, False]), []
    ui.user32 = SimpleNamespace(
        MsgWaitForMultipleObjectsEx=lambda *args: next(statuses),
        PeekMessageW=lambda *args: next(messages),
        TranslateMessage=lambda message: observed.append("translate"),
        DispatchMessageW=lambda message: observed.append("dispatch"),
    )
    process = SimpleNamespace(args=["configtest"], wait=lambda **kwargs: observed.append("exit"))
    ui.wait(process, 30)
    assert observed == ["translate", "dispatch", "exit"]


def test_native_ui_close_releases_wait_handle_even_if_unhook_fails(monkeypatch):
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    closed = []
    ui.hook, ui.process_handle = 5, 6
    ui.user32 = SimpleNamespace(UnhookWinEvent=lambda hook: False)
    ui.kernel32 = SimpleNamespace(CloseHandle=lambda handle: (closed.append(handle), True)[1])
    monkeypatch.setattr(ctypes, "get_last_error", lambda: 5, raising=False)
    with pytest.raises(OSError, match="release"):
        ui.close()
    assert closed == [6]
    assert ui.hook is None and ui.process_handle is None


def test_native_wait_handle_release_failure_is_not_silenced(monkeypatch):
    ui = configtest._Win32ConfigtestUI.__new__(configtest._Win32ConfigtestUI)
    ui.hook, ui.process_handle = None, 6
    ui.kernel32 = SimpleNamespace(CloseHandle=lambda handle: False)
    monkeypatch.setattr(ctypes, "get_last_error", lambda: 5, raising=False)
    with pytest.raises(OSError, match="wait handle"):
        ui.close()


class FakeRunnerUI(FakeUI):
    def __init__(self, deadline, *, show_error=False, fail=False):
        super().__init__()
        self.deadline, self.show_error, self.fail = deadline, show_error, fail
        self.closed = False
        self.process = None

    def watch(self, pid, inspect):
        for window in self.windows.values():
            window["pid"] = pid
        if self.show_error:
            inspect(10)

    def wait(self, process, timeout):
        self.process = process
        if self.fail:
            raise OSError("native wait failed")
        process.wait(timeout=max(self.deadline - time.monotonic(), 0.001))

    def close(self):
        self.closed = True


@pytest.mark.parametrize("exit_code,show_error", [(0, False), (7, True)])
def test_runner_preserves_actual_status_stdout_and_gui_diagnostics(tmp_path, monkeypatch, exit_code, show_error):
    ui = FakeRunnerUI(time.monotonic() + 5, show_error=show_error)
    monkeypatch.setattr(configtest, "_Win32ConfigtestUI", lambda deadline: ui)
    log = tmp_path / "configtest.log"
    argv = [sys.executable, "-c", f"print('child output'); raise SystemExit({exit_code})"]
    result = configtest.run_windows_configtest(argv, log, cwd=tmp_path, timeout=5)
    assert result.returncode == exit_code
    assert "child output" in result.stdout
    assert (ui.text in result.stdout) == show_error
    assert result.stdout == log.read_text(encoding="utf-8")
    assert ui.closed and ui.process.poll() is not None


def test_runner_does_not_accept_error_dialog_with_success_status(tmp_path, monkeypatch):
    ui = FakeRunnerUI(time.monotonic() + 5, show_error=True)
    monkeypatch.setattr(configtest, "_Win32ConfigtestUI", lambda deadline: ui)
    with pytest.raises(RuntimeError, match="displayed an error but exited successfully"):
        configtest.run_windows_configtest([sys.executable, "-c", "pass"], tmp_path / "error.log",
                                         cwd=tmp_path, timeout=5)
    assert ui.closed and ui.process.poll() is not None


@pytest.mark.parametrize("fail", [False, True])
def test_runner_timeout_or_api_failure_cleans_owned_child_and_retains_log(tmp_path, monkeypatch, fail):
    ui = FakeRunnerUI(time.monotonic() + 0.1, fail=fail)
    monkeypatch.setattr(configtest, "_Win32ConfigtestUI", lambda deadline: ui)
    log = tmp_path / "failure.log"
    error = OSError if fail else subprocess.TimeoutExpired
    with pytest.raises(error):
        configtest.run_windows_configtest([sys.executable, "-c", "import time; time.sleep(60)"],
                                         log, cwd=tmp_path, timeout=0.1)
    assert ui.closed and ui.process.poll() is not None
    assert "Configtest runner failed" in log.read_text(encoding="utf-8")


@pytest.mark.parametrize("timeout", [0, -1])
def test_runner_rejects_invalid_deadline_before_launch(tmp_path, timeout):
    with pytest.raises(ValueError, match="positive"):
        configtest.run_windows_configtest(["unused"], tmp_path / "unused.log", cwd=tmp_path, timeout=timeout)
    assert not (tmp_path / "unused.log").exists()


def test_node_uses_numbered_windows_logs_and_preserves_rejection(tmp_path, monkeypatch):
    node = node_module.UdbNode.__new__(node_module.UdbNode)
    node.log_dir = tmp_path
    node._command = lambda **kwargs: ["UnrealIRCd.exe", "-c"]
    node._process_cwd = lambda: tmp_path
    node._process_env = lambda: {"LOCAL": "test"}
    calls = []

    def run(argv, log_path, **kwargs):
        calls.append((log_path.name, kwargs))
        return subprocess.CompletedProcess(argv, 7, stdout="udb::propagator requires a valid server name")

    monkeypatch.setattr(node_module, "os", SimpleNamespace(name="nt"))
    monkeypatch.setattr(configtest, "run_windows_configtest", run)
    for _ in range(2):
        with pytest.raises(RuntimeError, match=r"(?s)configtest failed .*udb::propagator"):
            node._run_configtest()
    assert [name for name, _ in calls] == ["configtest-01.log", "configtest-02.log"]
    assert all(options == {"timeout": 30, "cwd": tmp_path, "env": {"LOCAL": "test"}}
               for _, options in calls)


def test_node_posix_configtest_path_remains_unchanged(tmp_path, monkeypatch):
    node = node_module.UdbNode.__new__(node_module.UdbNode)
    node._command = lambda **kwargs: ["ircd", "-c"]
    node._process_cwd = lambda: tmp_path
    node._process_env = lambda: None
    calls = []

    def run(argv, **kwargs):
        calls.append(kwargs)
        return subprocess.CompletedProcess(argv, 0, stdout="valid")

    monkeypatch.setattr(node_module, "os", SimpleNamespace(name="posix"))
    monkeypatch.setattr(node_module.subprocess, "run", run)
    node._run_configtest()
    assert calls == [{"text": True, "stdout": subprocess.PIPE, "stderr": subprocess.STDOUT,
                      "timeout": 30, "cwd": tmp_path, "env": None}]
