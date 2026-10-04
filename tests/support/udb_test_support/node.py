"""Disposable, bubblewrap-isolated real UnrealIRCd nodes for integration tests."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import socket
import subprocess
import time

from .runtime import OwnedProcess, Wire


BLOCKS = "NCISLK"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)


def _free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def _runtime_root() -> Path:
    configured = os.environ.get("UDB_TEST_IRCD_ROOT")
    return Path(configured) if configured else Path.home() / "unrealircd"


def _module_path() -> Path:
    configured = os.environ.get("UDB_MODULE_PATH")
    candidates = [Path(configured)] if configured else []
    repository = Path(__file__).resolve().parents[3]
    candidates.extend((repository / "src/udb.so", repository / "dist/udb.so"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "compiled UDB module unavailable; set UDB_MODULE_PATH or build src/udb.so"
    )


def _runtime_state_module() -> Path:
    repository = Path(__file__).resolve().parents[3]
    source = repository / "tests/udb_test_state.c"
    module = source.with_suffix(".so")
    if not source.is_file():
        raise FileNotFoundError(f"test-only runtime state module source unavailable: {source}")
    if module.is_file() and module.stat().st_mtime >= source.stat().st_mtime:
        return module

    configured = os.environ.get("UNREALIRCD_SRC_ROOT")
    candidate = repository.parents[3] if len(repository.parents) > 3 else repository
    source_root = Path(configured) if configured else (
        candidate if (candidate / "Makefile").is_file() else repository
    )
    result = subprocess.run(
        ["make", "custommodule", "MODULEFILE=udb/tests/udb_test_state"],
        cwd=source_root, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=120,
    )
    if result.returncode or not module.is_file():
        raise RuntimeError(f"test-only runtime state module build failed:\n{result.stdout}")
    return module


class IrcClient:
    """One registered loopback IRC user backed by the shared bounded Wire."""

    def __init__(self, host: str, port: int, nickname: str):
        self.nickname = nickname
        self.socket = socket.create_connection((host, port), timeout=5)
        self.wire = Wire(self.socket)
        try:
            self.send("PROTOCTL NAMESX")
            self.send(f"NICK {nickname}")
            self.send(f"USER {nickname} 0 * :{nickname}")
            self.wire.wait(
                lambda line: f" 001 {nickname} " in line,
                f"IRC welcome for {nickname}",
            )
        except BaseException:
            self.close()
            raise

    @property
    def lines(self) -> list[str]:
        return self.wire.lines

    def send(self, line: str) -> None:
        self.wire.send(line)

    def request(self, command: str, *, terminator=None, timeout=20) -> list[str]:
        """Return fresh replies through a protocol terminator or PING barrier."""
        start = len(self.lines)
        token = f"test-reply-{start}"
        self.send(command)
        if terminator is None:
            self.send(f"PING :{token}")
            terminator = lambda line: " PONG " in line and line.endswith(f" :{token}")
        self.wait(terminator, f"reply completion for {command}", start=start, timeout=timeout)
        return self.lines[start:]

    def wait(self, predicate, description: str, timeout: float = 5, start: int = 0) -> str:
        return self.wire.wait(predicate, description, timeout=timeout, start=start)

    def close(self) -> None:
        self.wire.close()


class UdbNode:
    def __init__(self, path: Path, *, ready: bool, prepare=None, expected_state="READY",
                 name="udb-test.test", sid="0T1", propagator=None, peers=(), outgoing=None, settings="",
                 ulines=(), runtime_state_fixture=False, oper_defaults=False, oper_auto_join=(),
                 vhost_flood_limit=None, tkl_monitor_exception=False):
        self.path = path
        self.name, self.sid = name, sid
        self.propagator = name if propagator is None else propagator
        self.peers, self.settings = tuple(peers), settings
        self.ulines = tuple(ulines)
        self.runtime_state_fixture = runtime_state_fixture
        self.oper_defaults = oper_defaults
        self.oper_auto_join = tuple(oper_auto_join)
        self.vhost_flood_limit = vhost_flood_limit
        if not isinstance(tkl_monitor_exception, bool):
            raise ValueError("tkl_monitor_exception must be a boolean")
        self.tkl_monitor_exception = tkl_monitor_exception
        if (vhost_flood_limit is not None and
                (isinstance(vhost_flood_limit, bool) or not isinstance(vhost_flood_limit, int) or
                 not 1 <= vhost_flood_limit <= 1000)):
            raise ValueError("vhost_flood_limit must be an integer from 1 to 1000")
        if any(not channel.startswith("#") or any(char in channel for char in ' \t\r\n";,')
               for channel in self.oper_auto_join):
            raise ValueError("oper-auto-join entries must be simple #channel names")
        if len(set(self.ulines)) != len(self.ulines) or not set(self.ulines) <= set(self.peers):
            raise ValueError("every unique U-line requires an explicit peer entry")
        self.outgoing = {} if outgoing is None else dict(outgoing)
        if not self.outgoing.keys() <= set(self.peers):
            raise ValueError("every outgoing server requires an explicit peer entry")
        self.runtime_root = _runtime_root()
        self.ircd = self.runtime_root / "bin/unrealircd"
        if not self.ircd.is_file() or not os.access(self.ircd, os.X_OK):
            raise FileNotFoundError(f"configured UnrealIRCd binary unavailable: {self.ircd}")
        if not shutil.which("bwrap"):
            raise RuntimeError("bubblewrap is required for isolated runtime integration tests")

        self.module_source = _module_path()
        self.data_dir = path / "runtime-data"
        self.temp_dir = path / "tmp"
        self.cache_dir = path / "cache"
        self.log_dir = path / "logs"
        self.modules_dir = path / "modules/third"
        for directory in (self.data_dir, self.temp_dir, self.cache_dir, self.log_dir, self.modules_dir):
            directory.mkdir(parents=True, exist_ok=True)

        self.client_port = _free_port()
        self.tls_port = _free_port()
        self.server_port = _free_port()
        shutil.copy2(self.module_source, self.modules_dir / "udb.so")
        if self.runtime_state_fixture:
            shutil.copy2(_runtime_state_module(), self.modules_dir / "udb_test_state.so")
        if ready:
            self._seed_ready()
        if prepare is not None:
            prepare(self.data_dir)
        self.config = path / "unrealircd.conf"
        self._write_config()
        self._run_configtest()

        self.process = OwnedProcess(self._command(foreground=True), self.log_dir / "ircd.log", cwd=path)
        self.clients: list[IrcClient] = []
        self.process.start()
        try:
            self._wait_for_listener()
            if expected_state is not None:
                self.wait_for_state(expected_state)
        except BaseException:
            self.close()
            raise

    def _seed_ready(self) -> None:
        for letter in BLOCKS:
            (self.data_dir / f"udb_{letter}.db").write_text(
                f"; UDB Block {letter} - Version 1\n; Generation: 1\n",
                encoding="ascii",
            )
        (self.data_dir / ".udb_state").write_text(
            "FORMAT=1\nSTATE=READY\nORIGIN=FRESH\nGENERATION=1\nLAST_SYNC=1787720000\n",
            encoding="ascii",
        )

    def _write_config(self) -> None:
        cloak = " ".join(f'"{key}";' for key in CLOAK_KEYS)
        links = []
        for peer in self.peers:
            outgoing = (f'outgoing {{ hostname "127.0.0.1"; port {self.outgoing[peer]}; }}'
                        if peer in self.outgoing else "")
            links.append(f'link {peer} {{ incoming {{ mask "127.0.0.1"; }} {outgoing} password "testlinkpassword"; class servers; }}')
        links = "\n".join(links)
        policy = f'propagator "{self.propagator}";' if self.propagator else ""
        ulines = "\n".join(f"    {name};" for name in self.ulines)
        ulines = f"ulines {{\n{ulines}\n}}" if ulines else ""
        runtime_state_module = (
            'loadmodule "third/udb_test_state";' if self.runtime_state_fixture else ""
        )
        oper_defaults = (
            'modes-on-oper "+xws"; snomask-on-oper "+c"; '
            'oper-vhost "$operlogin.$operclass.oper.test";'
            if self.oper_defaults else ""
        )
        oper_auto_join = (
            f'oper-auto-join "{",".join(self.oper_auto_join)}";'
            if self.oper_auto_join else ""
        )
        vhost_flood = (
            f'anti-flood {{ known-users {{ vhost-flood {self.vhost_flood_limit}:60; }} '
            f'unknown-users {{ vhost-flood {self.vhost_flood_limit}:60; }} }}'
            if self.vhost_flood_limit is not None else ""
        )
        tkl_monitor_exception = (
            'except ban { mask "lineoper@*"; type { gline; gzline; } }'
            if self.tkl_monitor_exception else ""
        )
        self.config.write_text(
            f'''include "{self.runtime_root}/conf/modules.default.conf";
include "{self.runtime_root}/conf/operclass.default.conf";
include "{self.runtime_root}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";
me {{ name "{self.name}"; info "UDB disposable test node"; sid "{self.sid}"; }}
admin {{ "UDB tests"; "local"; "udb@example.invalid"; }}
set {{
    kline-address "udb@example.invalid";
    default-server "{self.name}";
    network-name "UDB Tests";
    help-channel "#help";
    cloak-keys {{ {cloak} }}
    {oper_defaults}
    {oper_auto_join}
    {vhost_flood}
}}
class clients {{ pingfreq 60; maxclients 20; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 20; sendq 20M; }}
oper testoper {{ mask "127.0.0.1"; password "operpass"; operclass "netadmin-with-override"; class clients; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {self.client_port}; }}
listen {{ ip "127.0.0.1"; port {self.tls_port}; options {{ tls; }} }}
listen {{ ip "127.0.0.1"; port {self.server_port}; options {{ serversonly; }} }}
{links}
{ulines}
{tkl_monitor_exception}
loadmodule "cloak_sha256";
loadmodule "third/udb";
{runtime_state_module}
udb {{ {policy} {self.settings} }}
''',
            encoding="ascii",
        )

    def _command(self, *, foreground: bool) -> list[str]:
        runtime_data = self.runtime_root / "data"
        command = [
            "bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", str(self.path), str(self.path),
            "--bind", str(self.data_dir), str(runtime_data),
            "--bind", str(self.temp_dir), str(self.runtime_root / "tmp"),
            "--bind", str(self.cache_dir), str(self.runtime_root / "cache"),
            "--bind", str(self.log_dir), str(self.runtime_root / "logs"),
            "--ro-bind", str(self.modules_dir), str(self.runtime_root / "modules/third"),
            "--dev-bind", "/dev", "/dev", "--proc", "/proc",
            str(self.ircd), "-f", str(self.config),
        ]
        command.append("-F" if foreground else "-c")
        return command

    def _run_configtest(self) -> None:
        result = subprocess.run(
            self._command(foreground=False), text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
        )
        if result.returncode:
            raise RuntimeError(
                f"UnrealIRCd configtest failed ({result.returncode}):\n{result.stdout}"
            )

    def _wait_for_listener(self, timeout: float = 10) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.process.poll() is not None:
                raise RuntimeError(
                    f"UnrealIRCd exited ({self.process.process.returncode}):\n{self.log_text()}"
                )
            try:
                with socket.create_connection(("127.0.0.1", self.client_port), timeout=0.2):
                    return
            except OSError:
                time.sleep(0.05)
        raise TimeoutError(f"UnrealIRCd did not open its client listener:\n{self.log_text()}")

    @property
    def state_file(self) -> Path:
        return self.data_dir / ".udb_state"

    def state(self) -> dict[str, str]:
        try:
            lines = self.state_file.read_text(encoding="ascii").splitlines()
        except OSError as error:
            raise AssertionError(f"UDB state is unavailable: {self.state_file}") from error
        return dict(line.split("=", 1) for line in lines if "=" in line)

    def wait_for_state(self, expected: str, timeout: float = 15) -> dict[str, str]:
        deadline = time.monotonic() + timeout
        state: dict[str, str] = {}
        while time.monotonic() < deadline:
            if self.state_file.is_file():
                state = self.state()
                if state.get("STATE") == expected:
                    return state
            if self.process.process.poll() is not None:
                break
            time.sleep(0.05)
        raise TimeoutError(f"expected UDB STATE={expected}; state={state}\n{self.log_text()}")

    def client(self, nickname: str) -> IrcClient:
        client = IrcClient("127.0.0.1", self.client_port, nickname)
        self.clients.append(client)
        return client

    def log_text(self) -> str:
        try:
            return (self.log_dir / "ircd.log").read_text(encoding="utf-8", errors="replace")
        except OSError:
            return "<no UnrealIRCd log available>"

    def close(self) -> None:
        for client in reversed(self.clients):
            client.close()
        self.clients.clear()
        if hasattr(self, "process"):
            self.process.stop()

    def __enter__(self) -> "UdbNode":
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.close()
        return False
