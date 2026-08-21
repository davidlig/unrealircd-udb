#!/usr/bin/env python3
"""Regression: an identical C::#chan::modes INS must not revoke founder +q.

Reproduces the services topology: a fake services server (UDB propagator)
links to a one-node network and re-INSs the channel modes record. The UDB
module must apply channel modes only when the value changes, and must never
strip the founder rank (+q) as a side effect.
"""

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


ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

SERVICES_NAME = "udb-svc.test"
SERVICES_SID = "002"
IRCD_SID = "001"
LINK_PASSWORD = "udb-svc-link-password"
CHANNEL = "#vault"


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def sha256(password):
    return hashlib.sha256(password.encode("ascii")).hexdigest()


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";

me {{
    name "{name}";
    info "UDB isolated one-node integration harness";
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
link {SERVICES_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "{SERVICES_NAME}";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, configtest=False):
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
               "--bind", str(node), str(node),
               "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
               "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
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
        raise AssertionError(f"{self.nick}: no se recibió {description}; líneas={self.lines[start:]!r}")

    def request(self, command, terminator, description):
        start = len(self.lines)
        self.send(command)
        return self.wait_for(terminator, description, start)

    def close(self):
        try:
            self.send("QUIT :test complete")
        except OSError:
            pass
        finally:
            self.sock.close()


class FakeServices:
    """Minimal services server: links over S2S and pushes UDB mutations."""

    def __init__(self, host, port, name, sid, password, ircd_sid):
        self.sid = sid
        self.ircd_sid = ircd_sid
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{password}")
        self.send(f"PROTOCTL EAUTH={name}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 SID=" + sid)
        self.send(f"SERVER {name} 1 :UDB test services")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                raise AssertionError("services: server closed the connection")
            self.buffer += data.decode(errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = [line for line in self.lines if predicate(line)]
            if matches:
                return matches
            self.receive(deadline)
        raise AssertionError(f"services: no se recibió {description}; líneas={self.lines!r}")

    def wait_hel(self):
        self.wait_for(lambda line: line.startswith(f":{self.ircd_sid} DB {self.sid} HEL 4 "),
                      "UDB HEL del servidor")

    def hel_ack(self):
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")

    def send_ins(self, path, data):
        self.send(f"DB * INS {path} :{data}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def names(client):
    return client.request("NAMES " + CHANNEL, lambda line: " 366 " in line, "end of NAMES")


def wait_for_daemon(process, ports, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"daemon exited with status {process.returncode}")
        try:
            for host, port in ports:
                probe = socket.create_connection((host, port), timeout=0.25)
                probe.close()
            return
        except OSError:
            time.sleep(0.05)
            continue
    raise RuntimeError("daemon did not open its listeners")


def wait_for_file_content(path, needle, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if needle in path.read_text(errors="replace"):
                return True
        except FileNotFoundError:
            pass
        time.sleep(0.05)
    return False


def exercise(host, client_port, server_port, c_db):
    clients = []
    services = None
    try:
        alice = IrcClient(host, client_port, "alice-setup")
        clients.append(alice)
        alice.request("NICK alice:secret", lambda line: " NICK :alice" in line,
                      "cambio al nick registrado")
        alice.wait_for(lambda line: " MODE alice " in line and "+r" in line, "nick registration +r")
        alice.request("CAP REQ :multi-prefix", lambda line: " CAP " in line and
                      (" ACK " in line or " NAK " in line), "CAP reply")
        alice.request("JOIN " + CHANNEL, lambda line: " 366 " in line, "end of founder JOIN")
        require(any("~alice" in line for line in names(alice)),
                f"el fundador no recibió +q al entrar: {names(alice)!r}")

        services = FakeServices(host, server_port, SERVICES_NAME, SERVICES_SID, LINK_PASSWORD, IRCD_SID)
        services.wait_hel()
        services.hel_ack()

        # Phase 1: identical modes INS must be a no-op (no -ntM/+ntM churn,
        # and above all no founder +q removal).
        start = len(alice.lines)
        services.send_ins(f"C::{CHANNEL}::modes", "+ntM")
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            alice.receive(deadline)
            time.sleep(0.05)
        mode_traffic = [line for line in alice.lines[start:] if f"MODE {CHANNEL}" in line]
        require(not mode_traffic,
                f"INS idéntico de modes generó cambios de modos: {mode_traffic!r}")
        require(any("~alice" in line for line in names(alice)),
                f"INS idéntico de modes quitó el +q del fundador: {names(alice)!r}")

        # Phase 2: a changed modes value must apply, still without touching +q.
        start = len(alice.lines)
        services.send_ins(f"C::{CHANNEL}::modes", "+ntm")
        alice.wait_for(lambda line: f"MODE {CHANNEL}" in line and "+m" in line,
                       "aplicación del nuevo valor de modes", start=start)
        require(not any("-q" in line for line in alice.lines[start:]),
                f"cambio de modes revocó +q del fundador: {alice.lines[start:]!r}")
        require(any("~alice" in line for line in names(alice)),
                f"cambio de modes quitó el +q del fundador: {names(alice)!r}")
        require(wait_for_file_content(c_db, f"{CHANNEL}::modes +ntm", 5),
                f"el valor cambiado de modes no se persistió en {c_db}")

        # Phase 3: a channel-profile INS revokes and must restore the founder.
        start = len(alice.lines)
        services.send_ins(f"C::{CHANNEL}", "*123")
        alice.wait_for(lambda line: f"MODE {CHANNEL}" in line and "+q alice" in line,
                       "restauración del founder +q tras el INS del canal", start=start)
        require(any("-q alice" in line for line in alice.lines[start:]),
                f"el INS del canal no revocó el +q previo: {alice.lines[start:]!r}")
        require(any("~alice" in line for line in names(alice)),
                f"el INS del canal no restauró el +q del fundador: {names(alice)!r}")
        print("PASS: INS idéntico sin churn ni pérdida de +q, modos aplicados al cambiar y "
              "fundador restaurado tras el INS del perfil de canal")
    finally:
        if services:
            services.close()
        for client in clients:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD,
                        help="UnrealIRCd binary (default: UDB_TEST_IRCD_ROOT/bin/unrealircd)")
    parser.add_argument("--module", type=pathlib.Path, default=ROOT / "src/modules/third/udb/src/udb.so",
                        help="compiled UDB module")
    parser.add_argument("--timeout", type=int, default=15, help="daemon readiness timeout in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve the temporary node directory")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for the isolated PERMDATADIR mount namespace")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip(f"installed modules.default.conf is unavailable under {RUNTIME_ROOT}")

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-modes-ins-"))
    process = None
    try:
        node = root / "node"
        data = node / "data"
        data.mkdir(parents=True)
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(args.module, third_modules / "udb.so")
        (data / "udb_N.db").write_text(
            f"alice::pass sha256:{sha256('secret')}\n"
            "alice::challenge sha256\n"
            "alice::access 127.0.0.0/8\n",
            encoding="ascii")
        (data / "udb_C.db").write_text(
            f"{CHANNEL}::founder alice\n"
            f"{CHANNEL}::pass sha256:{sha256('chansecret')}\n"
            f"{CHANNEL}::challenge sha256\n"
            f"{CHANNEL}::modes +ntM\n",
            encoding="ascii")
        port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-one.test", IRCD_SID, port, server_port, tls_port, args.module, data)
        run_configtest(node, args.ircd, config)
        log = node / "ircd.log"
        with log.open("w") as output:
            process = subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=output,
                                       stderr=subprocess.STDOUT, text=True)
        wait_for_daemon(process, (("127.0.0.1", port), ("127.0.0.1", server_port)), args.timeout)
        exercise("127.0.0.1", port, server_port, data / "udb_C.db")
        return 0
    except EnvironmentUnavailable as exc:
        return skip(f"bwrap cannot create the required mount namespace: {exc}")
    except (AssertionError, OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        if root.exists():
            log = root / "node" / "ircd.log"
            if log.exists():
                print(f"--- daemon log ({log}) ---\n{log.read_text(errors='replace')}", file=sys.stderr)
        return 1
    finally:
        stop(process)
        if args.keep:
            print(f"Temporary files retained at: {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
