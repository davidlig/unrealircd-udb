#!/usr/bin/env python3
"""Integration tests for UDB_LNKOPT_DEBUG notice filtering for IRCOPs."""

import argparse
import hashlib
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    for candidate in (
        REPO_ROOT / "src" / "udb.so",
        REPO_ROOT / "dist" / "udb.so",
        RUNTIME_ROOT / "modules/third/udb.so",
    ):
        if candidate.is_file():
            return candidate
    return REPO_ROOT / "src" / "udb.so"


class EnvironmentUnavailable(Exception):
    pass


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def sha256(password):
    return hashlib.sha256(password.encode("ascii")).hexdigest()


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB debug notices integration harness";
    sid "{sid}";
}}
admin {{ "UDB harness"; "udb"; "udb@example.invalid"; }}
set {{
    kline-address "udb@example.invalid";
    default-server "{name}";
    network-name "UDB Harness";
    help-channel "#help";
    cloak-keys {{ "{CLOAK_KEYS[0]}"; "{CLOAK_KEYS[1]}"; "{CLOAK_KEYS[2]}"; }}
}}
class clients {{ pingfreq 60; maxclients 20; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 4; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "{name}";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, configtest=False):
    for sub in ("runtime-data", "tmp", "cache", "logs"):
        (node / sub).mkdir(parents=True, exist_ok=True)
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
               "--bind", str(node), str(node),
               "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
               "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
               "--bind", str(node / "cache"), str(RUNTIME_ROOT / "cache"),
               "--bind", str(node / "logs"), str(RUNTIME_ROOT / "logs"),
               "--ro-bind", str(node / "modules" / "third"), str(RUNTIME_ROOT / "modules/third"),
               "--dev-bind", "/dev", "/dev", "--proc", "/proc",
               str(ircd), "-f", str(config)]
    command.append("-c" if configtest else "-F")
    return command


def bwrap_unavailable(output):
    return any(text in output for text in ("Creating new namespace failed", "Operation not permitted",
                                            "No permissions to create a new namespace", "bwrap: "))


def run_configtest(node, ircd, config):
    result = subprocess.run(bwrap_command(node, ircd, config, configtest=True), text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    if result.returncode:
        if bwrap_unavailable(result.stdout):
            raise EnvironmentUnavailable(result.stdout.strip())
        raise RuntimeError(f"configtest failed for {config}:\n{result.stdout}")
    print(f"PASS: configtest {config.name} (generated config loads UDB)")


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


class IrcClient:
    def __init__(self, host, port, nick):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=3)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"NICK {nick}")
        self.send(f"USER {nick} 0 * :{nick}")
        self.wait_for(lambda line: " 001 " in line, "welcome")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                raise AssertionError(f"{self.nick}: server closed the connection")
            self.buffer += data.decode(errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, start=0, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = [line for line in self.lines[start:] if predicate(line)]
            if matches:
                return self.lines[start:]
            self.receive(deadline)
        raise AssertionError(f"{self.nick}: did not receive {description}; lines={self.lines[start:]!r}")

    def collect(self, timeout=1.0):
        deadline = time.monotonic() + timeout
        self.receive(deadline)
        return self.lines

    def close(self):
        try:
            self.send("QUIT :test complete")
        except OSError:
            pass
        finally:
            self.sock.close()


def wait_for_daemon(process, host, port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("daemon exited unexpectedly")
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError("daemon did not start listening in time")


def test_node(ircd, module, enable_debug):
    server_name = f"udb-{'debug' if enable_debug else 'nodebug'}.test"
    with tempfile.TemporaryDirectory(prefix=f"udb-debug-test-{'on' if enable_debug else 'off'}-") as temp_dir:
        node = pathlib.Path(temp_dir)
        (node / "data").mkdir()
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        (node / "modules" / "third").mkdir(parents=True)
        shutil.copy2(module, node / "modules" / "third" / "udb.so")

        # Configure Nick and Channel blocks
        (node / "data" / "udb_N.db").write_text(
            f"alice::pass sha256:{sha256('secret')}\n"
            "alice::oper netadmin\n",
            encoding="ascii")
        (node / "data" / "udb_C.db").write_text(
            "#test::founder alice\n"
            f"#test::pass sha256:{sha256('chansecret')}\n",
            encoding="ascii")

        if enable_debug:
            (node / "data" / "udb_L.db").write_text(
                f"{server_name}::options *1\n",
                encoding="ascii")
        else:
            (node / "data" / "udb_L.db").write_text(
                f"{server_name}::options *0\n",
                encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, server_name, "0D1", client_port, server_port, tls_port, module, node / "data")
        run_configtest(node, ircd, config)

        log = node / "ircd.log"
        process = None
        try:
            with log.open("w") as output:
                process = subprocess.Popen(bwrap_command(node, ircd, config),
                                           stdout=output, stderr=subprocess.STDOUT, text=True)
            wait_for_daemon(process, "127.0.0.1", client_port, 10)

            # Connect alice and identify to get +o
            alice = IrcClient("127.0.0.1", client_port, "guest")
            alice.send("NICK alice:secret")
            alice.wait_for(lambda line: " MODE alice " in line and "+o" in line, "oper grant +o")

            # Join channel to trigger UDB founder mode +q application
            start_idx = len(alice.lines)
            alice.send("JOIN #test chansecret")
            alice.wait_for(lambda line: " 366 " in line, "end of names")

            # Collect any notices delivered to the oper
            time.sleep(0.5)
            lines = alice.collect(timeout=1.0)
            oper_notices = [l for l in lines[start_idx:] if "udb." in l or "[UDB]" in l or "[UDB Debug]" in l]

            if enable_debug:
                if not any("udb." in l for l in oper_notices):
                    raise AssertionError(f"Expected udb notice when UDB_LNKOPT_DEBUG is enabled, got lines: {lines[start_idx:]}")
                print("PASS: IRCOP received standard UDB notices when UDB_LNKOPT_DEBUG (*1) is enabled")
            else:
                if oper_notices:
                    raise AssertionError(f"Expected NO UDB notices when UDB_LNKOPT_DEBUG is disabled, but received: {oper_notices}")
                print("PASS: IRCOP received zero UDB notices when UDB_LNKOPT_DEBUG is disabled")

            alice.close()
        finally:
            stop(process)


def main():
    parser = argparse.ArgumentParser(description="UDB debug notice filtering integration harness")
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD, help="path to unrealircd binary")
    parser.add_argument("--module", type=pathlib.Path,
                        default=find_module_path(),
                        help="path to compiled udb.so")
    args = parser.parse_args()

    if not args.ircd.is_file():
        print(f"SKIP: unrealircd binary not found at {args.ircd}")
        return 77
    if not args.module.is_file():
        print(f"SKIP: udb.so module not found at {args.module}")
        return 77

    # Test 1: Debug Disabled (*0) -> Oper receives NO notices
    test_node(args.ircd, args.module, enable_debug=False)

    # Test 2: Debug Enabled (*1) -> Oper receives [UDB Debug] notices
    test_node(args.ircd, args.module, enable_debug=True)

    print("ALL TESTS PASSED: UDB debug notices are properly restricted to UDB_LNKOPT_DEBUG listeners.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
