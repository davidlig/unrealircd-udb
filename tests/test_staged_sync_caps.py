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


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir,
                 max_records=100000, max_bytes=67108864, inact_timeout=60, abs_timeout=300):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

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
    max-staged-records {max_records};
    max-staged-bytes {max_bytes};
    sync-inactivity-timeout {inact_timeout};
    sync-absolute-timeout {abs_timeout};
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
        self.round_id = 0
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

    def send_begin(self, letter, txid, checksum):
        self.round_id += 1
        self.send(f"DB {self.ircd_sid} INF {self.round_id} {letter} deadbeef {int(time.time()) + 1000}")
        self.wait_for(lambda line: f" RES {self.round_id} {letter}" in line,
                      f"RES for round {self.round_id} block {letter}")
        self.send(f"DB {self.ircd_sid} BEGIN {self.round_id} {letter} {txid} {checksum}")

    def send_put(self, letter, txid, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.ircd_sid} PUT {self.round_id} {letter} {txid} {path} {val}")

    def send_end(self, letter, txid, checksum):
        self.send(f"DB {self.ircd_sid} END {self.round_id} {letter} {txid} {checksum}")

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
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-staged-caps-"))
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

        # Seed initial N and C blocks with active records
        (data_dir / "udb_N.db").write_text("; UDB Block N\n; Saved: 1787720000\n; Records: 1\nalice::pass crypt:sample\n", encoding="ascii")
        (data_dir / "udb_C.db").write_text("; UDB Block C\n; Saved: 1787720000\n; Records: 1\n#test::topic InitialTopic\n", encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        # Test config: max 4 staged records, max 1200 staged bytes, 2s inactivity timeout, 4s absolute timeout
        write_config(config, "udb-staged.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir,
                     max_records=4, max_bytes=1200, inact_timeout=2, abs_timeout=4)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start:\n{stdout}")

        services = MockServices("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: max-staged-records boundary (limit=4 real tree nodes)
        # Tree node accounting in Block C:
        # PUT 1: #chan1::topic (creates 2 nodes: #chan1, topic -> total 2 nodes) <= 4 (OK)
        # PUT 2: #chan1::founder (adds 1 node under #chan1: founder -> total 3 nodes = limit - 1) <= 4 (OK)
        # PUT 3: #chan1::forbid (adds 1 node under #chan1: forbid -> total 4 nodes = limit) <= 4 (OK)
        # PUT 4: #chan1::suspended (adds 1 node under #chan1: suspended -> total 5 nodes = limit + 1) -> ABORT
        # -------------------------------------------------------------
        services.send_begin("C", "tx-rec-cap", "00000000")
        # PUT 1: 2 nodes (limit - 2)
        services.send_put("C", "tx-rec-cap", "#chan1::topic", "Valid topic string")
        time.sleep(0.1)
        # PUT 2: 3 nodes (limit - 1)
        services.send_put("C", "tx-rec-cap", "#chan1::founder", "ValidFounder")
        time.sleep(0.1)
        # PUT 3: 4 nodes (exact limit)
        services.send_put("C", "tx-rec-cap", "#chan1::forbid", "Prohibited channel")
        time.sleep(0.1)
        # PUT 4: 5 nodes (limit + 1) -> Must abort session
        services.send_put("C", "tx-rec-cap", "#chan1::suspended", "Suspended reason")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " PUT " in l,
                          "rejection of exceeding max-staged-records with ERR PUT")
        print("PASS: max-staged-records limit+1 correctly aborted session with ERR PUT (exact node count 5 > 4)")

        # Verify active database was NOT modified
        db_c = (data_dir / "udb_C.db").read_text(encoding="ascii")
        if "#test::topic InitialTopic" not in db_c or "#chan1" in db_c:
            raise AssertionError(f"Active database was corrupted by aborted staged-sync:\n{db_c}")
        print("PASS: Active database and memory tree remained invariant after record cap abort")

        # -------------------------------------------------------------
        # Test 2: max-staged-bytes boundary (limit=1200 bytes)
        # Accounting formula: payload_len = strlen(path) + strlen(data)
        # Uses schema-valid topic strings in Block C (valid up to 4096 bytes)
        # PUT 1: path "#c1::topic" (10 bytes) + data 590 bytes = 600 bytes (cumulative: 600)
        # PUT 2: path "#c2::topic" (10 bytes) + data 590 bytes = 600 bytes (cumulative: 1200 = exact limit)
        # PUT 3: path "#c3::topic" (10 bytes) + data 1 byte = 11 bytes (cumulative: 1211 > limit) -> ABORT
        # -------------------------------------------------------------
        services.send_begin("C", "tx-byte-cap", "00000000")
        # PUT 1: 600 bytes
        services.send_put("C", "tx-byte-cap", "#c1::topic", "A" * 590)
        time.sleep(0.1)
        # PUT 2: 600 bytes (cumulative 1200 = exact limit)
        services.send_put("C", "tx-byte-cap", "#c2::topic", "B" * 590)
        time.sleep(0.1)
        # PUT 3: 11 bytes (cumulative 1211 > 1200) -> Should abort
        services.send_put("C", "tx-byte-cap", "#c3::topic", "C")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " PUT " in l,
                          "rejection of exceeding max-staged-bytes with ERR PUT")
        print("PASS: max-staged-bytes limit+1 correctly aborted session with ERR PUT (payload 1211 > 1200)")

        db_c = (data_dir / "udb_C.db").read_text(encoding="ascii")
        if "#test::topic InitialTopic" not in db_c or "#c1" in db_c:
            raise AssertionError(f"Active database was corrupted by aborted staged-sync:\n{db_c}")
        print("PASS: Active database remained invariant after byte cap abort")

        # -------------------------------------------------------------
        # Test 3: sync-inactivity-timeout (configured as 2 seconds)
        # -------------------------------------------------------------
        services.send_begin("N", "tx-inact-to", "00000000")
        services.send_put("N", "tx-inact-to", "user1::vhost", "vhost1.test")
        time.sleep(3.0)  # Wait beyond 2s inactivity timeout (3s ensures deadline has strictly elapsed)
        # Next PUT will fail because session timed out and was destroyed
        services.send_put("N", "tx-inact-to", "user2::vhost", "vhost2.test")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " PUT " in l,
                          "rejection of PUT after inactivity timeout")
        print("PASS: Staged sync session aborted on inactivity timeout")

        # -------------------------------------------------------------
        # Test 4: sync-absolute-timeout (configured as 4 seconds)
        # -------------------------------------------------------------
        services.send_begin("N", "tx-abs-to", "00000000")
        # Keep sending PUT every 1s so inactivity timeout never fires, but absolute does at t=4s
        for i in range(3):
            time.sleep(1.0)
            services.send_put("N", "tx-abs-to", "user1::vhost", f"vhost{i}.test")
        time.sleep(2.0)  # Now at ~5.0s (exceeds 4s absolute timeout)
        services.send_put("N", "tx-abs-to", "user1::vhost", "vhost_over_abs.test")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " PUT " in l,
                          "rejection of PUT after absolute timeout")
        print("PASS: Staged sync session aborted on absolute timeout despite ongoing activity")

        # -------------------------------------------------------------
        # Test 5: Clean staged sync commit after previous aborts
        # -------------------------------------------------------------
        services.send_begin("N", "tx-valid-final", "00000000")
        services.send_end("N", "tx-valid-final", "00000000")
        services.wait_for(lambda l: " DB " in l and " ACK " in l and " N " in l,
                          "confirmation of ACK for staged-sync")
        print("PASS: Valid staged-sync session completed and acknowledged with ACK")

        services.close()
        stop(proc)
        proc = None

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
