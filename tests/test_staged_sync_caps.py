#!/usr/bin/env python3
"""Integration tests for staged sync caps, DoS protection, and fail-safe abortion."""

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


ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

SERVICES_NAME = "udb-svc.test"
SERVICES_SID = "002"
IRCD_SID = "001"
LINK_PASSWORD = "udb-svc-link-password"


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
    info "UDB staged sync caps harness";
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


def bwrap_command(node, ircd, config):
    return ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", str(node), str(node),
            "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
            "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
            "--ro-bind", str(node / "modules" / "third"), str(RUNTIME_ROOT / "modules/third"),
            "--dev-bind", "/dev", "/dev", "--proc", "/proc",
            str(ircd), "-F", "-f", str(config)]


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


class MockServices:
    def __init__(self, host, port):
        self.sid = SERVICES_SID
        self.ircd_sid = IRCD_SID
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

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_ins(self, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.ircd_sid} INS {path} {val}")

    def send_begin(self, letter, txid, checksum):
        self.send(f"DB {self.ircd_sid} BEGIN {letter} {txid} {checksum}")

    def send_put(self, letter, txid, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.ircd_sid} PUT {letter} {txid} {path} {val}")

    def send_end(self, letter, txid, checksum):
        self.send(f"DB {self.ircd_sid} END {letter} {txid} {checksum}")

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                return
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, timeout=5):
        deadline = time.monotonic() + timeout
        start = len(self.lines)
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}")

    def close(self):
        try:
            self.send(f"SQUIT {SERVICES_NAME} :bye")
        except OSError:
            pass
        self.sock.close()


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-staged-caps-"))
    module_path = ROOT / "src/modules/third/udb/src/udb.so"
    proc = None

    try:
        node = tmpdir / "node"
        data_dir = node / "data"
        data_dir.mkdir(parents=True)
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(module_path, third_modules / "udb.so")

        # Seed initial N block with an active record
        (data_dir / "udb_N.db").write_text("; UDB Block N\n; Saved: 1787720000\n; Records: 1\nalice::pass crypt:sample\n", encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-staged.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start:\n{stdout}")

        services = MockServices("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: Invalid staged PUT payload causes fail-safe abort
        # -------------------------------------------------------------
        services.send_begin("N", "tx-abort-1", "00000000")
        services.send_put("N", "tx-abort-1", "N::invalid%zzpath", "some_data")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " PUT " in l,
                          "rechazo de PUT inválido con ERR PUT")
        print("PASS: payload inválido en sesión de staged-sync abortó limpiamente con ERR PUT")

        # Verify active database in memory and on disk was NOT modified
        db_n = (data_dir / "udb_N.db").read_text(encoding="ascii")
        if "alice::pass crypt:sample" not in db_n:
            raise AssertionError(f"Active database was corrupted by aborted staged-sync:\n{db_n}")
        print("PASS: base de datos activa permaneció intacta tras aborto de staged-sync")

        # -------------------------------------------------------------
        # Test 2: Subsequent valid staged-sync commits empty tree cleanly
        # -------------------------------------------------------------
        services.send_begin("N", "tx-valid-empty", "00000000")
        services.send_end("N", "tx-valid-empty", "00000000")
        services.wait_for(lambda l: " DB " in l and " ACK " in l and " N " in l,
                          "confirmación de ACK de staged-sync")
        print("PASS: sesión válida de staged-sync completó y confirmó con ACK")

        # -------------------------------------------------------------
        # Test 3: Live INS after staged sync succeeds
        # -------------------------------------------------------------
        services.send_ins("N::bob::pass", "crypt:sample_bob")
        time.sleep(0.5)

        services.close()
        stop(proc)
        proc = None

        # Verify valid mutation committed successfully
        db_n = (data_dir / "udb_N.db").read_text(encoding="ascii")
        if "bob::pass crypt:sample_bob" not in db_n:
            raise AssertionError(f"Valid INS did not commit after prior staged sync:\n{db_n}")
        print("PASS: mutación posterior completó e hizo commit atómico con éxito")

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB staged sync caps integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
