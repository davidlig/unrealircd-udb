#!/usr/bin/env python3
"""Integration tests for strict numeric parsing and INF invariant validation."""

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
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB strict numeric parsing harness";
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
    for sub in ("runtime-data", "tmp", "cache", "logs"):
        (node / sub).mkdir(parents=True, exist_ok=True)
    return ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", str(node), str(node),
            "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
            "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
            "--bind", str(node / "cache"), str(RUNTIME_ROOT / "cache"),
            "--bind", str(node / "logs"), str(RUNTIME_ROOT / "logs"),
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
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send(f"SERVER {SERVICES_NAME} 1 :UDB test services")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")
        self.send(f"DB {self.ircd_sid} HEL 4 ?")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_ins(self, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.ircd_sid} INS {path} {val}")

    def send_inf(self, letter, checksum, timestamp):
        self.send(f"DB {self.ircd_sid} INF {letter} {checksum} {timestamp}")

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


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    local_path = pathlib.Path(__file__).resolve().parent.parent / "src" / "udb.so"
    if local_path.is_file():
        return local_path
    runtime_path = RUNTIME_ROOT / "modules/third/udb.so"
    if runtime_path.is_file():
        return runtime_path
    return local_path


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-numeric-test-"))
    module_path = find_module_path()
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

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-num.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start:\n{stdout}")

        services = MockServices("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: INF frame with malformed/overflowing checksum or timestamp
        # -------------------------------------------------------------
        # Non-hex checksum
        services.send_inf("N", "ZZZZZZZZ", "1787720000")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with non-hex checksum")
        print("PASS: INF with non-hex checksum was rejected with ERR INF")

        # Negative timestamp
        services.send_inf("N", "A1B2C3D4", "-100")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with negative timestamp")
        print("PASS: INF with negative timestamp was rejected with ERR INF")

        # Leading plus in timestamp
        services.send_inf("N", "A1B2C3D4", "+1787720000")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with leading plus sign")
        print("PASS: INF with leading plus sign in timestamp was rejected with ERR INF")

        # Overflowing timestamp (exceeding signed 64-bit max: 9223372036854775807)
        services.send_inf("N", "A1B2C3D4", "9223372036854775808")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with INT64_MAX+1 timestamp")
        print("PASS: INF with INT64_MAX+1 timestamp was rejected with ERR INF")

        # ULLONG_MAX timestamp
        services.send_inf("N", "A1B2C3D4", "18446744073709551615")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with ULLONG_MAX timestamp")
        print("PASS: INF with ULLONG_MAX timestamp was rejected with ERR INF")

        # Huge overflowing timestamp
        services.send_inf("N", "A1B2C3D4", "999999999999999999999999999999999999")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INF " in l,
                          "rejection of INF with overflowing timestamp")
        print("PASS: INF with overflowing timestamp was rejected with ERR INF")

        # -------------------------------------------------------------
        # Test 2: INS numeric record validation with invalid numeric payloads
        # -------------------------------------------------------------
        # Empty numeric payload '*'
        services.send_ins("I::127.0.0.1::clones", "*")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rejection of empty numeric payload (*)")
        print("PASS: Empty numeric value (*) was rejected with ERR INS")

        # Non-digit in numeric payload (*123a)
        services.send_ins("I::127.0.0.1::clones", "*123a")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rejection of numeric payload with non-digit chars (*123a)")
        print("PASS: Numeric value with non-digits (*123a) was rejected with ERR INS")

        # Negative in numeric payload (*-5)
        services.send_ins("I::127.0.0.1::clones", "*-5")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rejection of negative numeric payload (*-5)")
        print("PASS: Negative numeric value (*-5) was rejected with ERR INS")

        # Overflowing numeric payload
        services.send_ins("I::127.0.0.1::clones", "*999999999999999999999999999999999999")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rejection of overflowing numeric payload")
        print("PASS: Numeric value with overflow was rejected with ERR INS")

        # Valid numeric payload (*5)
        services.send_ins("I::127.0.0.1::clones", "*5")
        time.sleep(0.3)

        services.close()
        stop(proc)
        proc = None

        # Verify disk persistence of the valid numeric record
        db_i = (data_dir / "udb_I.db").read_text(encoding="ascii")
        if "127.0.0.1::clones *5" not in db_i:
            raise AssertionError(f"Valid numeric record not found in udb_I.db:\n{db_i}")
        print("PASS: Valid numeric value (*5) was persisted successfully")

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB strict numeric integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
