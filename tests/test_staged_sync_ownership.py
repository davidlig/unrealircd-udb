#!/usr/bin/env python3
"""Integration tests for staged sync transaction ownership, TXID boundary enforcement, and cross-peer isolation."""

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
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

SERVICES_A_NAME = "udb-svca.test"
SERVICES_A_SID = "00A"
SERVICES_B_NAME = "udb-svcb.test"
SERVICES_B_SID = "00B"
IRCD_SID = "001"
LINK_PASSWORD = "udb-ownership-link-password"


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def compute_tree_checksum(records):
    """Computes standard UDB tree CRC32 digest over sorted lines."""
    if not records:
        return "00000000"
    lines = sorted([f"{p} {v}\n".encode("ascii") for p, v in records])
    return f"{zlib.crc32(b''.join(lines)) & 0xFFFFFFFF:08X}"


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB staged sync ownership harness";
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
class servers {{ pingfreq 60; connfreq 6; maxclients 8; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
link {SERVICES_A_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}
link {SERVICES_B_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}
ulines {{
    {SERVICES_A_NAME};
    {SERVICES_B_NAME};
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
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


class MockPeer:
    def __init__(self, name, sid, host, port):
        self.name = name
        self.sid = sid
        self.ircd_sid = IRCD_SID
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{LINK_PASSWORD}")
        self.send(f"PROTOCTL EAUTH={self.name}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send(f"SERVER {self.name} 1 :UDB peer {self.name}")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, f"{self.name} link handshake")
        self.send("EOS")
        self.send(f"DB {self.ircd_sid} HEL 4 ?")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_begin(self, letter, txid, checksum="00000000"):
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

    def wait_for(self, predicate, description, timeout=5, start=None):
        deadline = time.monotonic() + timeout
        start_idx = start if start is not None else len(self.lines)
        while time.monotonic() < deadline:
            for l in self.lines[start_idx:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}; lines={self.lines[start_idx:]}")

    def close(self):
        try:
            self.send(f"SQUIT {self.name} :bye")
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
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-staged-ownership-"))
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

        # Initial database seed
        (data_dir / "udb_C.db").write_text("; UDB Block C\n; Saved: 1787720000\n; Records: 1\n#test::topic InitialTopic\n", encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-ownership.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start:\n{stdout}")

        peer_a = MockPeer(SERVICES_A_NAME, SERVICES_A_SID, "127.0.0.1", server_port)
        peer_b = MockPeer(SERVICES_B_NAME, SERVICES_B_SID, "127.0.0.1", server_port)

        # -------------------------------------------------------------
        # TEST A: TXID maximum valid length (exactly 31 characters)
        # -------------------------------------------------------------
        txid_max = "x" * 31
        recs_a = [("#chan_max31::topic", "Valid topic for max 31 txid")]
        chk_a = compute_tree_checksum(recs_a)

        start_a = len(peer_a.lines)
        peer_a.send_begin("C", txid_max)
        peer_a.send_put("C", txid_max, recs_a[0][0], recs_a[0][1])
        peer_a.send_end("C", txid_max, chk_a)
        peer_a.wait_for(lambda l: " DB " in l and f" ACK C {txid_max} " in l,
                        "ACK for max 31-char TXID", start=start_a)
        print("PASS: TEST A - TXID maximum valid length (31 chars) completed BEGIN -> PUT -> END -> ACK")

        # -------------------------------------------------------------
        # TEST B: TXID maximum + 1 length (32 characters)
        # Expected: rejected with ERR BEGIN 2 (UDB_ERR_PARAMS); no session created;
        # immediate subsequent valid BEGIN succeeds.
        # -------------------------------------------------------------
        txid_over = "y" * 32
        start_a = len(peer_a.lines)
        peer_a.send_begin("C", txid_over)
        peer_a.wait_for(lambda l: " DB " in l and " ERR BEGIN 2 C" in l,
                        "rejection of 32-char TXID with ERR BEGIN 2", start=start_a)
        print("PASS: TEST B1 - TXID of 32 characters rejected with ERR BEGIN 2 (UDB_ERR_PARAMS)")

        # Immediate subsequent valid BEGIN succeeds cleanly
        recs_b2 = [("#chan_after_over::topic", "Clean topic")]
        chk_b2 = compute_tree_checksum(recs_b2)
        start_a = len(peer_a.lines)
        peer_a.send_begin("C", "tx-valid-after-over")
        peer_a.send_put("C", "tx-valid-after-over", recs_b2[0][0], recs_b2[0][1])
        peer_a.send_end("C", "tx-valid-after-over", chk_b2)
        peer_a.wait_for(lambda l: " DB " in l and " ACK C tx-valid-after-over " in l,
                        "ACK for valid session after rejected overlong TXID", start=start_a)
        print("PASS: TEST B2 - Immediate valid BEGIN succeeded after rejected overlong TXID")

        # -------------------------------------------------------------
        # TEST C: Two TXIDs with matching 30-char prefix + 'A' vs + 'B' (31 chars)
        # Verifies no truncation occurs that would cause them to collide.
        # -------------------------------------------------------------
        prefix = "z" * 30
        txid_ca = prefix + "A"
        txid_cb = prefix + "B"
        recs_c = [("#chan_c::topic", "Correct TXID payload")]
        chk_c = compute_tree_checksum(recs_c)

        start_a = len(peer_a.lines)
        peer_a.send_begin("C", txid_ca)
        # Sending PUT with txid_cb against active session txid_ca should fail with ERR PUT 5 (UDB_ERR_NO_SYNC)
        peer_a.send_put("C", txid_cb, "#chan_c::topic", "Wrong TXID payload")
        peer_a.wait_for(lambda l: " DB " in l and " ERR PUT 5 C" in l,
                        "rejection of mismatched sibling TXID", start=start_a)

        # But sending PUT with correct txid_ca succeeds
        start_a = len(peer_a.lines)
        peer_a.send_begin("C", txid_ca) # Start fresh session after abort
        peer_a.send_put("C", txid_ca, recs_c[0][0], recs_c[0][1])
        peer_a.send_end("C", txid_ca, chk_c)
        peer_a.wait_for(lambda l: " DB " in l and f" ACK C {txid_ca} " in l,
                        "ACK for exact 31-char TXID A", start=start_a)
        print("PASS: TEST C - Two 31-char TXIDs differing only in final character are distinct without truncation")

        # -------------------------------------------------------------
        # TEST D: Competing BEGIN (Peer A creates session; Peer B attempts BEGIN)
        # Expected: Peer B receives ERR BEGIN 4 (UDB_ERR_SYNC_ACTIVE); Peer A session remains alive and finishes.
        # -------------------------------------------------------------
        recs_d = [("#chan_owner_a::topic", "Topic from Owner A")]
        chk_d = compute_tree_checksum(recs_d)

        start_a = len(peer_a.lines)
        start_b = len(peer_b.lines)

        peer_a.send_begin("C", "tx-owner-a")
        time.sleep(0.1)
        peer_b.send_begin("C", "tx-competing-b")

        peer_b.wait_for(lambda l: " DB " in l and (" ERR BEGIN 4 C" in l or " ERR BEGIN 6 C" in l),
                        "rejection of competing BEGIN with ERR BEGIN 4/6", start=start_b)
        print("PASS: TEST D1 - Competing Peer B BEGIN rejected with ERR BEGIN 4/6")

        # Peer A continues and completes its active session successfully
        peer_a.send_put("C", "tx-owner-a", recs_d[0][0], recs_d[0][1])
        peer_a.send_end("C", "tx-owner-a", chk_d)
        peer_a.wait_for(lambda l: " DB " in l and " ACK C tx-owner-a " in l,
                        "ACK for Peer A completed staged transaction", start=start_a)
        print("PASS: TEST D2 - Owning Peer A successfully completed transaction after Peer B's competing attempt")

        # -------------------------------------------------------------
        # TEST E: Foreign PUT (Peer A creates session; Peer B attempts PUT)
        # Expected: Peer B receives ERR PUT 5/6; Peer A session intact and completes.
        # -------------------------------------------------------------
        recs_e = [("#chan_legit::topic", "Legitimate Topic From A")]
        chk_e = compute_tree_checksum(recs_e)

        start_a = len(peer_a.lines)
        start_b = len(peer_b.lines)

        peer_a.send_begin("C", "tx-owner-e")
        time.sleep(0.1)
        peer_b.send_put("C", "tx-owner-e", "#chan_hijack::topic", "Malicious Injected Topic")

        peer_b.wait_for(lambda l: " DB " in l and (" ERR PUT 5 C" in l or " ERR PUT 6 C" in l),
                        "rejection of foreign PUT with ERR PUT 5/6", start=start_b)
        print("PASS: TEST E1 - Foreign Peer B PUT rejected with ERR PUT 5/6")

        # Peer A continues its transaction with legitimate record
        peer_a.send_put("C", "tx-owner-e", recs_e[0][0], recs_e[0][1])
        peer_a.send_end("C", "tx-owner-e", chk_e)
        peer_a.wait_for(lambda l: " DB " in l and " ACK C tx-owner-e " in l,
                        "ACK for Peer A after foreign PUT attempt", start=start_a)

        db_c = (data_dir / "udb_C.db").read_text(encoding="ascii")
        if "#chan_legit::topic Legitimate Topic From A" not in db_c:
            raise AssertionError(f"Legitimate record missing in database snapshot:\n{db_c}")
        if "#chan_hijack" in db_c:
            raise AssertionError(f"Malicious record from Peer B was committed into database:\n{db_c}")
        print("PASS: TEST E2 - Database committed only Peer A records; Foreign Peer B injection blocked")

        # -------------------------------------------------------------
        # TEST F: Foreign END (Peer A creates session; Peer B attempts END)
        # Expected: Peer B receives ERR END 5/6; Peer A session intact and finishes.
        # -------------------------------------------------------------
        recs_f = [("#chan_f::topic", "Topic for Test F")]
        chk_f = compute_tree_checksum(recs_f)

        start_a = len(peer_a.lines)
        start_b = len(peer_b.lines)

        peer_a.send_begin("C", "tx-owner-f")
        peer_a.send_put("C", "tx-owner-f", recs_f[0][0], recs_f[0][1])
        time.sleep(0.1)

        # Peer B attempts to send END on A's session
        peer_b.send_end("C", "tx-owner-f", "00000000")
        peer_b.wait_for(lambda l: " DB " in l and (" ERR END 5 C" in l or " ERR END 6 C" in l),
                        "rejection of foreign END with ERR END 5/6", start=start_b)
        print("PASS: TEST F1 - Foreign Peer B END rejected with ERR END 5/6")

        # Peer A completes the transaction cleanly
        peer_a.send_end("C", "tx-owner-f", chk_f)
        peer_a.wait_for(lambda l: " DB " in l and " ACK C tx-owner-f " in l,
                        "ACK for Peer A after foreign END attempt", start=start_a)
        print("PASS: TEST F2 - Owning Peer A successfully completed and committed transaction after foreign END attempt")

        peer_a.close()
        peer_b.close()
        stop(proc)
        proc = None

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB staged sync ownership tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
