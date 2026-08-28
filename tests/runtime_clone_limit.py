#!/usr/bin/env python3
"""Integration tests for UDB clone limit and quit_clones enforcement."""

import argparse
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


def write_config(path, name, sid, client_port, server_port, tls_port, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB clone limit integration harness";
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

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                return
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

    def wait_for_disconnect(self, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
                if not data:
                    return self.lines
                self.buffer += data.decode(errors="replace")
                while "\r\n" in self.buffer:
                    line, self.buffer = self.buffer.split("\r\n", 1)
                    if line.startswith("PING "):
                        self.send("PONG " + line.split(" ", 1)[1])
                    elif line:
                        self.lines.append(line)
            except socket.timeout:
                pass
        raise AssertionError(f"{self.nick}: expected disconnect but connection stayed open; lines={self.lines!r}")

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


def test_clone_limit(ircd, module):
    server_name = "udb-clones.test"
    with tempfile.TemporaryDirectory(prefix="udb-clone-test-") as temp_dir:
        node = pathlib.Path(temp_dir)
        (node / "data").mkdir()
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        (node / "modules" / "third").mkdir(parents=True)
        shutil.copy2(module, node / "modules" / "third" / "udb.so")

        # Configure global clone limit = 3 and custom quit message
        custom_quit = "Demasiadas conexiones simultaneas (limite global)"
        (node / "data" / "udb_S.db").write_text(
            f"clones *3\n"
            f"quit_clones {custom_quit}\n",
            encoding="ascii"
        )
        for letter in ("N", "C", "I", "L", "K"):
            (node / "data" / f"udb_{letter}.db").write_text(
                f"; UDB Block {letter} - Version 1\n", encoding="ascii")
        (node / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, server_name, "0C1", client_port, server_port, tls_port, node / "data")
        run_configtest(node, ircd, config)

        log = node / "ircd.log"
        process = None
        try:
            with log.open("w") as output:
                process = subprocess.Popen(bwrap_command(node, ircd, config),
                                           stdout=output, stderr=subprocess.STDOUT, text=True)
            wait_for_daemon(process, "127.0.0.1", client_port, 10)

            # Connect 3 clients - all 3 must succeed
            c1 = IrcClient("127.0.0.1", client_port, "user1")
            c1.wait_for(lambda line: " 001 " in line, "welcome user1")
            print("PASS: client 1 connected successfully")

            c2 = IrcClient("127.0.0.1", client_port, "user2")
            c2.wait_for(lambda line: " 001 " in line, "welcome user2")
            print("PASS: client 2 connected successfully")

            c3 = IrcClient("127.0.0.1", client_port, "user3")
            c3.wait_for(lambda line: " 001 " in line, "welcome user3")
            print("PASS: client 3 connected successfully (clones *3 accepts 3 connections)")

            # Connect client 4 - must be rejected with custom quit_clones message
            c4 = IrcClient("127.0.0.1", client_port, "user4")
            lines = c4.wait_for_disconnect(timeout=5)
            error_lines = [l for l in lines if custom_quit in l or "ERROR" in l or "QUIT" in l]
            if not any(custom_quit in l for l in error_lines):
                raise AssertionError(f"Expected quit message '{custom_quit}' for 4th clone, got: {lines}")
            print("PASS: client 4 was rejected with custom quit_clones message")

            # Disconnect client 1
            c1.close()
            time.sleep(0.5)

            # Connect client 5 - should now succeed since active count is back to 2
            c5 = IrcClient("127.0.0.1", client_port, "user5")
            c5.wait_for(lambda line: " 001 " in line, "welcome user5")
            print("PASS: client 5 connected successfully after client 1 disconnected")

            c2.close()
            c3.close()
            c5.close()
        finally:
            stop(process)


def main():
    parser = argparse.ArgumentParser(description="Test UDB clone limits.")
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    args = parser.parse_args()

    if not args.ircd.exists():
        print(f"SKIP: unrealircd binary not found at {args.ircd}")
        return 77
    if not args.module.exists():
        print(f"SKIP: udb.so module not found at {args.module}")
        return 77

    try:
        test_clone_limit(args.ircd, args.module)
    except EnvironmentUnavailable as e:
        print(f"SKIP: {e}")
        return 77

    print("All clone limit tests passed successfully!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
