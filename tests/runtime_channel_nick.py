#!/usr/bin/env python3
"""Isolated one-node UDB nick and channel runtime integration harness."""

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


def write_config(path, name, sid, client_port, tls_port, module, dbdir):
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
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "{name}";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, configtest=False):
    # Keep the installed daemon immutable while isolating its mutable runtime paths.
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


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def names(client):
    return client.request("NAMES #vault", lambda line: " 366 " in line, "end of NAMES")


def wait_for_daemon(process, host, port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"daemon exited with status {process.returncode}")
        try:
            probe = socket.create_connection((host, port), timeout=0.25)
        except OSError:
            time.sleep(0.05)
            continue
        probe.close()
        return
    raise RuntimeError("daemon did not open its client listener")


def malformed_persisted_paths_rejected(log):
    text = log.read_text(errors="replace")
    return (text.count("Skipping malformed persisted record in block N") == 4 and
            "Skipping overlong persisted record" in text)


def wait_for_log(predicate, log, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate(log):
            return True
        time.sleep(0.05)
    return predicate(log)


def exercise(host, port):
    clients = []
    try:
        alice = IrcClient(host, port, "alice-setup")
        clients.append(alice)
        alice.request("NICK alice:secret", lambda line: " NICK :alice" in line,
                      "cambio al nick registrado")
        alice.wait_for(lambda line: " MODE alice " in line and "+r" in line, "nick registration +r")
        whois = alice.request("WHOIS alice", lambda line: " 318 " in line, "end of WHOIS")
        require(any("alice.test" in line for line in whois),
                f"WHOIS de alice no contiene vhost UDB: {whois!r}")

        alice.request("CAP REQ :multi-prefix", lambda line: " CAP " in line and
                      (" ACK " in line or " NAK " in line), "CAP reply")
        alice.request("JOIN #vault", lambda line: " 366 " in line, "end of founder JOIN")
        founder_names = names(alice)
        require(any("~alice" in line for line in founder_names),
                f"el fundador no recibió +q: {founder_names!r}")
        require(not any("@alice" in line for line in founder_names),
                f"el fundador recibió +o además de +q: {founder_names!r}")

        bob = IrcClient(host, port, "bob")
        clients.append(bob)
        rejected_nick = bob.request("NICK alice:wrong",
                                    lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                                    "rechazo de contraseña de nick inválida")
        require(any("registered" in line.lower() or "password" in line.lower() for line in rejected_nick),
                f"credencial de nick inválida no fue rechazada por UDB: {rejected_nick!r}")

        rejected_join = bob.request("JOIN #vault wrong", lambda line: " 475 " in line,
                                    "rechazo de contraseña de canal inválida")
        require(any(" 475 " in line for line in rejected_join),
                f"contraseña inválida no fue rechazada: {rejected_join!r}")
        bob.request("JOIN #vault chansecret", lambda line: " 366 " in line, "end of password JOIN")
        bob_names = names(bob)
        require(any("&bob" in line for line in bob_names),
                f"autenticación de canal no concedió +a: {bob_names!r}")
        require(not any("@bob" in line for line in bob_names),
                f"autenticación de canal concedió +o: {bob_names!r}")

        alice.request("NICK alice2", lambda line: " NICK :alice2" in line, "cambio de nick")
        mode_reply = alice.wait_for(lambda line: " MODE alice2 " in line and "-r" in line,
                                    "eliminación de +r al salir del nick")
        require(any("-r" in line for line in mode_reply),
                f"+r persistió tras salir del nick: {mode_reply!r}")
        require(any("-t" in line or "-rt" in line for line in mode_reply),
                f"el vhost UDB persistió tras salir del nick: {mode_reply!r}")
        print("PASS: nick sha256 +r/vhost, fundador solo +q, JOIN sha256 +a sin +o y credenciales inválidas rechazadas")
    finally:
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

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-one-node-"))
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
            "alice::access 127.0.0.0/8\n"
            "alice::vhost alice.test\n"
            "::leading invalid\n"
            "alice::::double invalid\n"
            "alice:::triple invalid\n"
            "alice:: trailing\n" +
            ("x" * 4096) + " value\n",
            encoding="ascii")
        (data / "udb_C.db").write_text(
            "#vault::founder alice\n"
            f"#vault::pass sha256:{sha256('chansecret')}\n"
            "#vault::challenge sha256\n",
            encoding="ascii")
        port, tls_port = free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-one.test", "0A1", port, tls_port, args.module, data)
        run_configtest(node, args.ircd, config)
        log = node / "ircd.log"
        with log.open("w") as output:
            process = subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=output,
                                       stderr=subprocess.STDOUT, text=True)
        wait_for_daemon(process, "127.0.0.1", port, args.timeout)
        require(wait_for_log(malformed_persisted_paths_rejected, log, args.timeout),
                "malformed and overlong persisted UDB paths were not rejected with warnings")
        print("PASS: malformed and overlong persisted UDB paths were skipped without aborting the block load")
        exercise("127.0.0.1", port)
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
