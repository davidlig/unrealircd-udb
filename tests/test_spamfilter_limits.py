#!/usr/bin/env python3
"""Integration tests for UDB Spamfilter SPAMFILTER_MAX mathematical boundaries."""

import argparse
import base64
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
    info "UDB spamfilter limits harness";
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
    def __init__(self, host, port, ircd_sid=IRCD_SID):
        self.sid = SERVICES_SID
        self.ircd_sid = ircd_sid
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
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            self.send(f"DB {self.ircd_sid} INF 1 {b} 00000000 0")
        time.sleep(0.2)

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_ins(self, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.ircd_sid} INS {path} {val}")

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(16384)
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

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}")

    def query_block(self, letter):
        start_idx = len(self.lines)
        self.send(f"DB {self.ircd_sid} RES 1 {letter}")
        begin_line = self.wait_for(lambda l: " DB " in l and f" BEGIN 1 {letter} " in l, f"BEGIN {letter} frame", start=start_idx, timeout=5)
        end_line = self.wait_for(lambda l: " DB " in l and f" END 1 {letter} " in l, f"END {letter} frame", start=start_idx, timeout=5)
        checksum = end_line.strip().split()[-1]
        records = {}
        for l in self.lines[start_idx:]:
            if " DB " in l and f" PUT 1 {letter} " in l:
                parts = l.split(f" PUT 1 {letter} ", 1)[1].split(" ", 2)
                path = parts[1]
                data = parts[2]
                if data.startswith(":"):
                    data = data[1:]
                records[path] = data
        return checksum, records

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
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-spamfilter-limits-"))
    module_path = find_module_path()

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
        write_config(config, "udb-spf.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.2)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start:\n{stdout}")

        services = MockServices("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: SPAMFILTER_MAX - 1 (3071 bytes) -> Accepted
        # -------------------------------------------------------------
        # -------------------------------------------------------------
        # Test 1: SPAMFILTER_MAX - 1 (3071 bytes) -> Accepted
        # -------------------------------------------------------------
        raw_3071 = "s3071_" + ("a" * 3062) + "end"  # exactly 3071 bytes
        b64_3071 = "b64%3A" + base64.b64encode(raw_3071.encode("ascii")).decode("ascii")
        services.send_ins(f"K::F::{b64_3071}::reason", "Reason3071")
        time.sleep(0.15)

        # -------------------------------------------------------------
        # Test 2: SPAMFILTER_MAX (3072 bytes) -> Accepted
        # -------------------------------------------------------------
        raw_3072 = "s3072_" + ("b" * 3063) + "end"  # exactly 3072 bytes
        b64_3072 = "b64%3A" + base64.b64encode(raw_3072.encode("ascii")).decode("ascii")
        services.send_ins(f"K::F::{b64_3072}::reason", "Reason3072")
        time.sleep(0.15)

        # Verify active tree in memory
        _, records_k = services.query_block("K")
        if f"F::{b64_3071}::reason" not in records_k or records_k[f"F::{b64_3071}::reason"] != "Reason3071":
            raise AssertionError("SPAMFILTER_MAX - 1 (3071B) was not stored in active tree")
        if f"F::{b64_3072}::reason" not in records_k or records_k[f"F::{b64_3072}::reason"] != "Reason3072":
            raise AssertionError("SPAMFILTER_MAX (3072B) was not stored in active tree")

        print("PASS: SPAMFILTER_MAX - 1 (3071B) and SPAMFILTER_MAX (3072B) accepted and stored in active tree")

        # -------------------------------------------------------------
        # Test 3: SPAMFILTER_MAX + 1 (3073 bytes) -> Rejected cleanly with ERR INS
        # -------------------------------------------------------------
        raw_3073 = "s3073_" + ("c" * 3064) + "end"  # exactly 3073 bytes
        b64_3073 = "b64%3A" + base64.b64encode(raw_3073.encode("ascii")).decode("ascii")
        start = len(services.lines)
        services.send_ins(f"K::F::{b64_3073}::reason", "Reason3073")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l,
                          "rejection of SPAMFILTER_MAX + 1 (3073B)", start=start, timeout=5)

        print("PASS: SPAMFILTER_MAX + 1 (3073B) rejected cleanly with ERR INS")

        # -------------------------------------------------------------
        # Test 4: Plain raw pattern > SPAMFILTER_MAX without base64 -> Rejected
        # -------------------------------------------------------------
        start = len(services.lines)
        services.send_ins("K::F::" + ("plain_" * 600) + "::reason", "ReasonPlain")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l,
                          "rejection of oversized plain spamfilter pattern", start=start)

        print("PASS: Oversized plain pattern > 3072B rejected cleanly with ERR INS")

        services.close()
        stop(proc)
        proc = None

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB Spamfilter SPAMFILTER_MAX tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
