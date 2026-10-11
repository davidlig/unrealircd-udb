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
    extension = "dll" if os.name == "nt" else "so"
    candidates.extend((repository / f"src/udb.{extension}", repository / f"dist/udb.{extension}"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        f"compiled UDB module unavailable; set UDB_MODULE_PATH or build src/udb.{extension}"
    )


def _runtime_state_module() -> Path:
    repository = Path(__file__).resolve().parents[3]
    source = repository / "tests/udb_test_state.c"
    module = source.with_suffix(".dll" if os.name == "nt" else ".so")
    if not source.is_file():
        raise FileNotFoundError(f"test-only runtime state module source unavailable: {source}")
    if module.is_file() and module.stat().st_mtime >= source.stat().st_mtime:
        return module

    configured = os.environ.get("UNREALIRCD_SRC_ROOT")
    candidate = repository.parents[3] if len(repository.parents) > 3 else repository
    source_root = Path(configured) if configured else (
        candidate if (candidate / "Makefile").is_file() else repository
    )
    command = (["build.bat", "CUSTOMMODULE", "MODULEFILE=udb/tests/udb_test_state"]
               if os.name == "nt" else ["make", "custommodule", "MODULEFILE=udb/tests/udb_test_state"])
    result = subprocess.run(command, cwd=source_root, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120, shell=(os.name == "nt"))
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
        self.ircd = self.runtime_root / ("bin/UnrealIRCd.exe" if os.name == "nt" else "bin/unrealircd")
        if not self.ircd.is_file() or (os.name != "nt" and not os.access(self.ircd, os.X_OK)):
            raise FileNotFoundError(f"configured UnrealIRCd binary unavailable: {self.ircd}")
        if os.name != "nt" and not shutil.which("bwrap"):
            raise RuntimeError("bubblewrap is required for isolated runtime integration tests")

        self.module_source = _module_path()
        self.data_dir = path / ("data" if os.name == "nt" else "runtime-data")
        self.temp_dir = path / "tmp"
        self.cache_dir = path / "cache"
        self.log_dir = path / "logs"
        self.modules_dir = path / "modules/third"
        for directory in (self.data_dir, self.temp_dir, self.cache_dir, self.log_dir, self.modules_dir):
            directory.mkdir(parents=True, exist_ok=True)
        if os.name == "nt":
            shutil.copytree(self.runtime_root / "modules", path / "modules", dirs_exist_ok=True)

        self.client_port = _free_port()
        self.tls_port = _free_port()
        self.server_port = _free_port()
        if os.name == "nt":
            shutil.copy2(self.module_source, self.modules_dir / "udb.dll")
        else:
            shutil.copy2(self.module_source, self.modules_dir / "udb.so")
        if self.runtime_state_fixture:
            suffix = ".dll" if os.name == "nt" else ".so"
            shutil.copy2(_runtime_state_module(), self.modules_dir / f"udb_test_state{suffix}")
        if prepare is not None:
            prepare(self.data_dir)
        self.config = path / ("conf/unrealircd.conf" if os.name == "nt" else "unrealircd.conf")
        if os.name == "nt":
            self.config.parent.mkdir(parents=True, exist_ok=True)
            for name in ("modules.default.conf", "operclass.default.conf", "snomasks.default.conf"):
                shutil.copy2(self.runtime_root / "conf" / name, self.config.parent / name)
            self._create_test_tls_certificate()
        self._write_config()
        (path / "bin").mkdir(parents=True, exist_ok=True)
        self._run_configtest()

        self.process = OwnedProcess(self._command(foreground=True), self.log_dir / "ircd.log",
                                    cwd=self._process_cwd(), env=self._process_env())
        self.clients: list[IrcClient] = []
        self.process.start()
        try:
            self._wait_for_listener()
            if expected_state is not None:
                self.wait_for_state(expected_state)
        except BaseException:
            self.close()
            raise

    def _write_config(self) -> None:
        cloak = " ".join(f'"{key}";' for key in CLOAK_KEYS)
        links = []
        for peer in self.peers:
            outgoing = (f'outgoing {{ hostname "127.0.0.1"; port {self.outgoing[peer]}; options {{ autoconnect; }} }}'
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
        tls_listener = f'listen {{ ip "127.0.0.1"; port {self.tls_port}; options {{ tls; }} }}' if self.tls_port else ""
        if os.name == "nt":
            include_lines = '\n'.join(f'include "conf/{name}";' for name in
                                       ("modules.default.conf", "operclass.default.conf", "snomasks.default.conf"))
        else:
            config_includes = [self.runtime_root / "conf/modules.default.conf",
                               self.runtime_root / "conf/operclass.default.conf",
                               self.runtime_root / "conf/snomasks.default.conf"]
            include_lines = "\n".join(f'include "{item.as_posix()}";' for item in config_includes)
        self.config.write_text(
            f'''{include_lines}
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
{tls_listener}
listen {{ ip "127.0.0.1"; port {self.server_port}; options {{ serversonly; }} }}
{links}
{ulines}
{tkl_monitor_exception}
loadmodule "cloak_sha256";
loadmodule "third/udb";
{runtime_state_module}
udb {{ {policy} {self.settings} }}
''',
            encoding="utf-8",
        )

    def _create_test_tls_certificate(self) -> None:
        tls_dir = self.config.parent / "tls"
        tls_dir.mkdir(parents=True, exist_ok=True)
        ca_bundle = self.runtime_root / "conf/tls/curl-ca-bundle.crt"
        if ca_bundle.is_file():
            shutil.copy2(ca_bundle, tls_dir / ca_bundle.name)
        libraries = os.environ.get("UDB_TEST_LIBRARIES_ROOT")
        candidates = [Path(libraries) / "openssl/bin/openssl.exe"] if libraries else []
        candidates.append(self.runtime_root / "openssl.exe")
        candidates.append(Path(shutil.which("openssl") or ""))
        openssl = next((item for item in candidates if item.is_file()), None)
        if openssl is None:
            raise FileNotFoundError(
                "OpenSSL is required for the disposable Windows IRC test listener; "
                "set UDB_TEST_LIBRARIES_ROOT"
            )
        env = os.environ.copy()
        env["OPENSSL_CONF"] = "NUL"
        result = subprocess.run(
            [str(openssl), "req", "-x509", "-newkey", "rsa:2048",
             "-keyout", str(tls_dir / "server.key.pem"),
             "-out", str(tls_dir / "server.cert.pem"), "-sha256", "-days", "1",
             "-nodes", "-subj", "/CN=udb-test.test"],
            cwd=self.path, env=env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=30,
        )
        if result.returncode:
            raise RuntimeError(f"could not create disposable test TLS certificate:\n{result.stdout}")

    def _process_cwd(self) -> Path:
        if os.name == "nt":
            # UnrealIRCd's Windows GUI wrapper always changes from bin\ to
            # its parent before initializing the daemon and reading relative paths.
            return self.path / "bin"
        return self.path

    def _process_env(self) -> dict[str, str] | None:
        if os.name != "nt":
            return None
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join((str(self.runtime_root), str(self.runtime_root / "bin"), env.get("PATH", "")))
        return env

    def _command(self, *, foreground: bool) -> list[str]:
        if os.name == "nt":
            config_arg = self.config.relative_to(self.path).as_posix()
            return [str(self.ircd), "-f", config_arg, "-F" if foreground else "-c"]
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
        if os.name == "nt":
            from .windows_configtest import run_windows_configtest

            self._configtest_sequence = getattr(self, "_configtest_sequence", 0) + 1
            result = run_windows_configtest(
                self._command(foreground=False),
                self.log_dir / f"configtest-{self._configtest_sequence:02d}.log",
                timeout=30, cwd=self._process_cwd(), env=self._process_env(),
            )
        else:
            result = subprocess.run(
                self._command(foreground=False), text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30, cwd=self._process_cwd(),
                env=self._process_env(),
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

    def wait_for_state(self, expected: str, timeout: float = 15):
        """Readiness is a runtime protocol property, not a legacy text marker."""
        if expected != "READY":
            raise ValueError("use server DBQ STATUS for bootstrap assertions")
        # Local BIND completion is consumed by the daemon event within 250 ms.
        # One finite grace avoids treating a just-opened listener as readiness.
        time.sleep(0.35)
        client = self.client("readiness")
        replies = client.request("OPER testoper operpass")
        assert any(" 381 " in line for line in replies), replies
        replies = client.request("UDB STATUS", timeout=timeout)
        assert any("Database readiness: READY" in line for line in replies), replies
        return {"STATE": "READY"}

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
