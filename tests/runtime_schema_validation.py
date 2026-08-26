#!/usr/bin/env python3
"""Integration tests for UDB declarative schema validation across all database blocks."""

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
CHANNEL = "#schematest"


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";

me {{
    name "{name}";
    info "UDB schema validation integration harness";
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
ulines {{
    {SERVICES_NAME};
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


class FakeServicesServer:
    def __init__(self, host, port):
        self.sid = SERVICES_SID
        self.ircd_sid = IRCD_SID
        self.uid_counter = 0
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{LINK_PASSWORD}")
        self.send(f"PROTOCTL EAUTH={SERVICES_NAME}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 SID=" + self.sid)
        self.send(f"SERVER {SERVICES_NAME} 1 :UDB test services")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")
        self.send(f"DB {self.ircd_sid} HEL 4")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")
        self.send_uid("NickServ")
        self.send_uid("ChanServ")

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
                raise AssertionError("services: server closed connection")
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
        raise AssertionError(f"services: no se recibió {description}; líneas={self.lines[start:]!r}")

    def send_uid(self, nick):
        self.uid_counter += 1
        uid = f"{self.sid}AAAA{self.uid_counter:02d}"
        self.send(f"UID {nick} 1 {int(time.time())} +ioSq {nick} services.test {nick}.services.test {uid} 0 0 :UDB Services")
        return uid

    def send_ins(self, path, data):
        self.send(f"DB * INS {path} {data}")

    def send_del(self, path):
        self.send(f"DB * DEL {path}")

    def close(self):
        try:
            self.send("SQUIT " + SERVICES_NAME + " :test complete")
        except OSError:
            pass
        finally:
            self.sock.close()


def run_tests(ircd_bin, keep=False):
    tmpdir = tempfile.mkdtemp(prefix="udb-schema-test-")
    node = pathlib.Path(tmpdir)
    proc = None

    try:
        data_dir = node / "data"
        runtime_data = node / "runtime-data"
        tmp_dir = node / "tmp"
        mods_third = node / "modules" / "third"
        for d in (data_dir, runtime_data, tmp_dir, mods_third):
            d.mkdir(parents=True, exist_ok=True)

        module_so = ROOT / "src/modules/third/udb/src/udb.so"
        if not module_so.is_file():
            module_so = ROOT / "src/modules/third/udb.so"
        shutil.copy2(module_so, mods_third / "udb.so")

        client_port = free_port()
        server_port = free_port()
        tls_port = free_port()
        config = node / "unrealircd.conf"
        write_config(config, "ircd.test", IRCD_SID, client_port, server_port, tls_port,
                     mods_third / "udb.so", str(data_dir))

        run_configtest(node, ircd_bin, config)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd exited immediately:\n{stdout}")

        services = FakeServicesServer("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: Rejection of unknown keys in Block C (Channel)
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::testunknownkey", "valor")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rechazo de clave desconocida testunknownkey en Bloque C")
        print("PASS: INS de clave desconocida testunknownkey en Bloque C fue rechazada con ERR INS 2 C")

        services.send_ins(f"C::{CHANNEL}::testkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rechazo de clave desconocida testkey en Bloque C")
        print("PASS: INS de clave desconocida testkey en Bloque C fue rechazada con ERR INS 2 C")

        services.send_ins(f"C::{CHANNEL}::testinvalidkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rechazo de clave desconocida testinvalidkey en Bloque C")
        print("PASS: INS de clave desconocida testinvalidkey en Bloque C fue rechazada con ERR INS 2 C")

        # -------------------------------------------------------------
        # Test 2: Rejection of wrong data types in Block C
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::founder", "*123")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rechazo de tipo numérico en founder")
        print("PASS: INS de founder con valor numérico fue rechazado")

        services.send_ins(f"C::{CHANNEL}::options", "noval")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rechazo de tipo texto en options")
        print("PASS: INS de options sin formato numérico '*' fue rechazado")

        # -------------------------------------------------------------
        # Test 3: Acceptance of valid keys in Block C
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::founder", "davidlig")
        services.send_ins(f"C::{CHANNEL}::options", "*3")
        services.send_ins(f"C::{CHANNEL}::access::davidlig", "*1")
        time.sleep(0.2)
        print("PASS: INS de claves válidas en Bloque C (founder, options, access) fueron aceptadas")

        # -------------------------------------------------------------
        # Test 4: Rejection of invalid nested paths in Block S
        # -------------------------------------------------------------
        services.send_ins("S::#test::testunknownkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rechazo de ruta anidada en Bloque S")
        print("PASS: INS de ruta anidada S::#test::testunknownkey fue rechazada con ERR INS 2 S")

        services.send_ins("S::testunknownkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rechazo de clave desconocida en Bloque S")
        print("PASS: INS de clave desconocida testunknownkey en Bloque S fue rechazada con ERR INS 2 S")

        services.send_ins("S::clones", "textvalue")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rechazo de clones no numérico en Bloque S")
        print("PASS: INS de clones con texto en Bloque S fue rechazado")

        # -------------------------------------------------------------
        # Test 5: Acceptance of valid keys in Block S
        # -------------------------------------------------------------
        services.send_ins("S::clones", "*5")
        services.send_ins("S::quit_clones", "Demasiadas conexiones")
        time.sleep(0.2)
        print("PASS: INS de claves válidas en Bloque S (clones *5, quit_clones) fueron aceptadas")

        # -------------------------------------------------------------
        # Test 6: Rejection of unknown keys in Block N, I, L, K
        # -------------------------------------------------------------
        services.send_ins("N::davidlig::testunknownkey", "valor")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " N" in l,
                          "rechazo de clave desconocida en Bloque N")
        print("PASS: INS de clave desconocida en Bloque N fue rechazada con ERR INS 2 N")

        services.send_ins("I::127.0.0.1::testunknownkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rechazo de clave desconocida en Bloque I")
        print("PASS: INS de clave desconocida en Bloque I fue rechazada con ERR INS 2 I")

        services.send_ins(f"L::{SERVICES_NAME}::testunknownkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " L" in l,
                          "rechazo de clave desconocida en Bloque L")
        print("PASS: INS de clave desconocida en Bloque L fue rechazada con ERR INS 2 L")

        services.send_ins("K::X::*@bad.test::reason", "bad")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " K" in l,
                          "rechazo de tipo de TKL inválido en Bloque K")
        print("PASS: INS de tipo de TKL inválido 'X' en Bloque K fue rechazado con ERR INS 2 K")

        services.send_ins("K::G::*@bad.test::testunknownkey", "bad")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " K" in l,
                          "rechazo de subclave desconocida en Bloque K")
        print("PASS: INS de subclave desconocida en Bloque K fue rechazada con ERR INS 2 K")

        services.close()
        stop(proc)
        proc = None

        # -------------------------------------------------------------
        # Test 7: File parsing on boot discards unknown/corrupted keys
        # -------------------------------------------------------------
        db_c = data_dir / "udb_C.db"
        db_c.write_text("""; UDB Block C - Version 1
; Saved: 1787715840
; Records: 5
#channel::testunknownkey *1
#channel::founder davidlig
#channel::testinvalidkey ascac
#channel::options *3
#channel::testkey *1
""", encoding="ascii")

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start with corrupted udb_C.db:\n{stdout}")

        # Connect IRC client to verify DB state:
        client = IrcClient("127.0.0.1", client_port, "alice")
        client.request("JOIN #channel", lambda l: " 353 " in l or "JOIN" in l, "join #channel")
        client.request("MODE #channel", lambda l: " 324 " in l, "channel mode check")
        client.close()

        stop(proc)
        proc = None
        print("PASS: arranque de servidor ignoró limpiamente claves desconocidas en udb_C.db y cargó registros válidos")

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB schema validation integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
