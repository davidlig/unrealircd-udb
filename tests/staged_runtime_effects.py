#!/usr/bin/env python3
"""Prove staged N/K snapshots change live clients on the receiving server."""

import argparse
import hashlib
import os
import pathlib
import secrets
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


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_ports(count):
    socks = []
    ports = []
    for _ in range(count):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        socks.append(s)
        ports.append(s.getsockname()[1])
    for s in socks:
        s.close()
    return ports


def free_port():
    return free_ports(1)[0]


def sha256(password):
    return hashlib.sha256(password.encode("ascii")).hexdigest()


def write_config(path, name, sid, client_port, server_port, tls_port, peer, peer_port, dbdir, autoconnect,
                 link_password):
    outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                'options { autoconnect; } }\n') if autoconnect else ""
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB staged runtime effects harness";
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
link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
 {outgoing}    password "{link_password}";
    class servers;
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "udb-a.test";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, configtest=False):
    for sub in ("runtime-data", "tmp", "cache", "logs"):
        (node / sub).mkdir(parents=True, exist_ok=True)
    return ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", str(node), str(node),
            "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
            "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
            "--bind", str(node / "cache"), str(RUNTIME_ROOT / "cache"),
            "--bind", str(node / "logs"), str(RUNTIME_ROOT / "logs"),
            "--ro-bind", str(node / "modules" / "third"), str(RUNTIME_ROOT / "modules/third"),
            "--dev-bind", "/dev", "/dev", "--proc", "/proc", str(ircd), "-f", str(config),
            "-c" if configtest else "-F"]


def bwrap_unavailable(output):
    return any(text in output for text in ("Creating new namespace failed", "Operation not permitted",
                                            "No permissions to create a new namespace", "bwrap: "))


def run_configtest(node, ircd, config):
    result = subprocess.run(bwrap_command(node, ircd, config, True), text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    if result.returncode:
        if bwrap_unavailable(result.stdout):
            raise EnvironmentUnavailable(result.stdout.strip())
        raise RuntimeError(f"configtest failed for {config}:\n{result.stdout}")


def stop(processes):
    for process in processes:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
    for process in processes:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wait_for_daemon(process, port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"daemon exited with status {process.returncode}")
        try:
            probe = socket.create_connection(("127.0.0.1", port), timeout=0.25)
        except OSError:
            time.sleep(0.05)
            continue
        probe.close()
        return
    raise RuntimeError("daemon did not open its client listener")


class IrcClient:
    def __init__(self, port, nick, password=None):
        self.nick = nick
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(0.25)
        self.buffer = ""
        self.lines = []
        self.send(f"NICK {nick}:{password}" if password else f"NICK {nick}")
        self.send(f"USER {nick} 0 * :{nick}")
        self.wait_for(lambda line: " 001 " in line, "welcome")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return True
            if not data:
                return False
            self.buffer += data.decode(errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)
        return True

    def wait_for(self, predicate, description, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(predicate(line) for line in self.lines):
                return
            if not self.receive(deadline):
                break
        raise AssertionError(f"{self.nick}: no {description}; lines={self.lines!r}")

    def close(self):
        try:
            self.send("QUIT :test complete")
        except OSError:
            pass
        self.sock.close()


def wait_for_link(processes, logs, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(process.poll() is not None for process in processes):
            return False
        if all("Server linked:" in log.read_text(errors="replace") and " is now synced" in log.read_text(errors="replace")
               for log in logs):
            return True
        time.sleep(0.25)
    return False


def wait_for_oper_revocation(client, timeout):
    client.wait_for(lambda line: f" MODE {client.nick} " in line and "-o" in line,
                    "UDB oper revocation", timeout)


def fresh_client_rejected(port, timeout):
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(0.25)
    try:
        sock.sendall(b"NICK banned\r\nUSER banned 0 * :banned\r\n")
        deadline = time.monotonic() + timeout
        received = ""
        while time.monotonic() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                return " 001 " not in received
            received += data.decode(errors="replace")
            if any(code in received for code in (" 432 ", " 433 ", " 437 ")):
                return True
            # UnrealIRCd may complete registration before applying a Q-line, then
            # visibly forces the prohibited nick away instead of closing the socket.
            if " NICK :Guest" in received:
                return True
        return False
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    parser.add_argument("--timeout", type=int, default=15, help="per-stage wait time in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve temporary node directories")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for separate PERMDATADIR mount namespaces")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip(f"installed modules.default.conf is unavailable under {RUNTIME_ROOT}")

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-staged-runtime-effects-"))
    processes, alice = [], None
    try:
        a, b = root / "node-a", root / "node-b"
        for node in (a, b):
            (node / "data").mkdir(parents=True)
            (node / "runtime-data").mkdir()
            (node / "tmp").mkdir()
            (node / "modules" / "third").mkdir(parents=True)
            shutil.copy2(args.module, node / "modules" / "third" / "udb.so")

        # B starts with an active UDB oper; A's newer snapshot deliberately omits it.
        b_n = b / "data" / "udb_N.db"
        a_n = a / "data" / "udb_N.db"
        b_n.write_text(f"alice::pass sha256:{sha256('secret')}\nalice::oper netadmin\n", encoding="ascii")
        a_n.write_text(f"alice::pass sha256:{sha256('secret')}\n", encoding="ascii")
        (b / "data" / "udb_K.db").touch()
        (a / "data" / "udb_K.db").write_text(
            "G::*@127.0.0.1::reason staged loopback ban\n"
            "Q::banned::reason staged fresh-client rejection\n", encoding="ascii")
        (a / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")
        (b / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787710000\n", encoding="ascii")
        old_time = time.time() - 120
        for db in (b_n, b / "data" / "udb_K.db", b / "data" / ".udb_state"):
            os.utime(db, (old_time, old_time))

        a_client, a_server, a_tls, b_client, b_server, b_tls = free_ports(6)
        a_conf, b_conf = a / "unrealircd.conf", b / "unrealircd.conf"
        link_password = "udb-test-" + secrets.token_hex(32)
        write_config(a_conf, "udb-a.test", "0A1", a_client, a_server, a_tls, "udb-b.test", b_server,
                     a / "data", True, link_password)
        write_config(b_conf, "udb-b.test", "0B1", b_client, b_server, b_tls, "udb-a.test", a_server,
                     b / "data", False, link_password)
        run_configtest(a, args.ircd, a_conf)
        run_configtest(b, args.ircd, b_conf)

        logs = (a / "ircd.log", b / "ircd.log")
        with logs[1].open("w") as output:
            processes.append(subprocess.Popen(bwrap_command(b, args.ircd, b_conf), stdout=output,
                                               stderr=subprocess.STDOUT, text=True))
        wait_for_daemon(processes[0], b_client, args.timeout)
        alice = IrcClient(b_client, "alice", "secret")
        alice.wait_for(lambda line: " MODE alice " in line and "+o" in line, "initial UDB oper grant")
        print("PASS: B granted alice +o from its active N block before staged sync")

        with logs[0].open("w") as output:
            processes.append(subprocess.Popen(bwrap_command(a, args.ircd, a_conf), stdout=output,
                                               stderr=subprocess.STDOUT, text=True))
        if not wait_for_link(processes, logs, args.timeout):
            return skip("A-B S2S link was not observed; this is not a PASS")
        wait_for_oper_revocation(alice, args.timeout)
        print("PASS: A's staged N snapshot visibly revoked alice's live +o on B")
        if not fresh_client_rejected(b_client, args.timeout):
            raise AssertionError("a fresh loopback client was neither disconnected nor rejected or renamed by staged K lines")
        print("PASS: A's staged K snapshot visibly rejected a fresh loopback client on B")
        return 0
    except EnvironmentUnavailable as exc:
        return skip(f"bubblewrap isolation is unavailable: {exc}")
    except (AssertionError, OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        if alice:
            alice.close()
        stop(processes)
        if args.keep:
            print(f"Temporary files retained at: {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
