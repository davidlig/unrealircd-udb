#!/usr/bin/env python3
"""Integration test for UDB4 mutation sequencing, gap detection, and self-repair.

Phase 2 Test:
1. Verifies in-order sequenced mutations are applied and increment sequence.
2. Verifies duplicate / stale sequence mutations are dropped idempotently.
3. Verifies sequence gap causes follower to reject mutation, enter DEGRADED state,
   and trigger automatic self-repair via HEL 4 refresh and reconciliation.
4. Verifies staged snapshot transfer restores convergence and advances sequence.
5. Verifies subsequent sequenced mutations resume normally.
"""

import os
import pathlib
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import zlib

RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
LINK_PASSWORD = "testlinkpassword"


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


def compute_tree_checksum(records):
    """Computes standard UDB tree CRC32 digest over sorted lines."""
    if not records:
        return "00000000"
    lines = sorted([f"{p} {v}\n".encode("ascii") for p, v in records])
    return f"{zlib.crc32(b''.join(lines)) & 0xFFFFFFFF:08X}"


def write_config(path, name, sid, ports, dbdir, propagator=None):
    udb_prop = f'    propagator "{propagator}";\n' if propagator is not None else ""
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB test node";
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
class clients {{ pingfreq 60; maxclients 50; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 10; sendq 20M; }}
allow {{ mask "*@*"; class clients; maxperip 50; }}
oper testoper {{
    mask "*@*";
    password "operpass";
    operclass "netadmin-with-override";
    class clients;
}}
listen {{ ip "127.0.0.1"; port {ports[0]}; }}
listen {{ ip "127.0.0.1"; port {ports[1]}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {ports[2]}; options {{ tls; }} }}

link services.test {{
    incoming {{ mask "*@*"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}

loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
{udb_prop}}}
''', encoding="ascii")


def bwrap_command(node, ircd, config):
    for sub in ("runtime-data", "tmp", "cache", "logs", "modules"):
        (node / sub).mkdir(parents=True, exist_ok=True)
    (node / "modules" / "third").mkdir(parents=True, exist_ok=True)
    src_mod = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
    dest_mod = node / "modules" / "third" / "udb.so"
    if src_mod.exists() and not dest_mod.exists():
        shutil.copy(src_mod, dest_mod)
    return ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", "/tmp", "/tmp",
            "--bind", str(node), str(node),
            "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
            "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
            "--bind", str(node / "cache"), str(RUNTIME_ROOT / "cache"),
            "--bind", str(node / "logs"), str(RUNTIME_ROOT / "logs"),
            "--ro-bind", str(node / "modules" / "third"), str(RUNTIME_ROOT / "modules/third"),
            "--dev-bind", "/dev", "/dev", "--proc", "/proc",
            str(ircd), "-F", "-f", str(config)]


def wait_for_daemon(process, host, port, timeout=10):
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
    raise RuntimeError("daemon did not open its listener")


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    time.sleep(0.3)


class MockPeer:
    def __init__(self, name, sid, host, port, target_sid, propagator_advertised=None):
        self.name = name
        self.sid = sid
        self.target_sid = target_sid
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.round_id = 0
        self.send_raw(f"PASS :{LINK_PASSWORD}")
        self.send_raw(f"PROTOCTL EAUTH={self.name}")
        self.send_raw("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send_raw(f"SERVER {self.name} 1 :UDB peer {self.name}")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, f"{self.name} link handshake")
        self.send("EOS")
        prop = propagator_advertised if propagator_advertised is not None else "?"
        self.send(f"DB {self.target_sid} HEL 4 {prop} 0000000000000001 OCL")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
        self.send(f"DB {self.target_sid} HEL 4 ACK {prop} 0000000000000001 OCL")

    def send_inventory(self, checksums=None, timestamp=0, watermark_seq=0):
        self.round_id += 1
        checksums = checksums or {}
        for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
            checksum, block_timestamp = checksums.get(letter, ("00000000", timestamp))
            self.send(f"DB {self.target_sid} INF {self.round_id} {letter} {checksum} {block_timestamp} {watermark_seq}")

    def send_begin(self, round_id, letter, txid, checksum, watermark_seq=0):
        self.send(f"DB {self.target_sid} BEGIN {round_id} {letter} {txid} {checksum} {watermark_seq}")

    def send_put(self, round_id, letter, txid, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB {self.target_sid} PUT {round_id} {letter} {txid} {path} {val}")

    def send_end(self, round_id, letter, txid, checksum, watermark_seq=0):
        self.send(f"DB {self.target_sid} END {round_id} {letter} {txid} {checksum} {watermark_seq}")

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            except OSError:
                return
            if not data:
                return
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                self.lines.append(line)

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines[start:]:
                if predicate(line):
                    return line
            self.receive(deadline)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines[start:]}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class MockClient:
    def __init__(self, host, port, nick="testuser"):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"USER {nick} 0 * :Test User")
        self.send(f"NICK {nick}")
        self.wait_for(lambda line: " 001 " in line, f"{nick} 001 welcome")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            except OSError:
                return
            if not data:
                return
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                self.lines.append(line)
                if line.startswith("PING "):
                    cookie = line.split(" ", 1)[1]
                    self.send(f"PONG {cookie}")

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines[start:]:
                if predicate(line):
                    return line
            self.receive(deadline)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines[start:]}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    if not shutil.which("bwrap"):
        print("SKIP: bwrap not available")
        return 77

    ircd_bin = DEFAULT_IRCD
    if not ircd_bin.exists():
        print(f"SKIP: ircd binary not found at {ircd_bin}")
        return 77

    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb_gap_test_"))
    node_dir = tmpdir / "follower"
    db_dir = node_dir / "runtime-data"
    cfg_file = node_dir / "unrealircd.conf"
    ports = free_ports(3)

    node_dir.mkdir(parents=True, exist_ok=True)
    write_config(cfg_file, "follower.test", "00F", ports, db_dir, propagator="services.test")

    proc = subprocess.Popen(bwrap_command(node_dir, ircd_bin, cfg_file))
    try:
        wait_for_daemon(proc, "127.0.0.1", ports[0])

        # Step 1: Connect services as authorized propagator and bootstrap empty DB
        services = MockPeer("services.test", "00S", "127.0.0.1", ports[1], "00F", propagator_advertised="services.test")
        services.send_inventory()
        time.sleep(0.5)

        # Oper connects and verifies READY and sync OK
        oper = MockClient("127.0.0.1", ports[0], "oper1")
        oper.send("OPER testoper operpass")
        oper.wait_for(lambda l: " 381 " in l, "Oper login")
        oper.send("UDB STATUS")
        oper.wait_for(lambda l: "Database readiness: READY" in l, "Follower READY")
        oper.wait_for(lambda l: "UDB synchronization: OK" in l, "Follower sync OK")

        # Step 2: In-order sequenced mutation 1
        print("Testing in-order mutation application (seq=1)...")
        start = len(oper.lines)
        services.send("DB * INS 0000000000000001 1 N::user1::vhost user1.org")
        time.sleep(0.3)
        oper.send("DBQ N::user1::vhost")
        oper.wait_for(lambda l: "user1.org" in l, "user1 vhost applied", start=start)
        print("PASS: seq=1 applied cleanly.")

        # Step 3: Duplicate mutation (seq=1 again with different value)
        print("Testing duplicate mutation drop (seq=1)...")
        services.send("DB * INS 0000000000000001 1 N::user1::vhost user1_dup.org")
        time.sleep(0.3)
        start = len(oper.lines)
        oper.send("DBQ N::user1::vhost")
        line = oper.wait_for(lambda l: "user1" in l, "user1 vhost query", start=start)
        assert "user1.org" in line and "user1_dup.org" not in line, f"Duplicate was incorrectly applied: {line}"
        print("PASS: duplicate seq=1 dropped idempotently without replacing value.")

        # Step 4: Sequence gap detection (skip seq 2, send seq 3)
        print("Testing sequence gap detection (seq=3, expected seq=2)...")
        srv_start = len(services.lines)
        services.send("DB * INS 0000000000000001 3 N::user3::vhost user3.org")
        time.sleep(0.3)

        # Mutation 3 must NOT be applied
        start = len(oper.lines)
        oper.send("DBQ N::user3::vhost")
        oper.wait_for(lambda l: "Block not found" in l, "user3 not found on gap", start=start)

        # Follower must transition to DEGRADED
        start = len(oper.lines)
        oper.send("UDB STATUS")
        oper.wait_for(lambda l: "UDB synchronization: DEGRADED" in l, "Follower status DEGRADED", start=start)
        print("PASS: mutation 3 isolated, follower entered DEGRADED status.")

        # Follower must trigger self-repair by re-announcing HEL 4 to authorized propagator
        print("Waiting for follower self-repair trigger (HEL 4 refresh)...")
        services.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "Follower re-HEL trigger", start=srv_start)
        print("PASS: Follower sent HEL 4 trigger to upstream propagator.")

        # Step 5: Services sends inventory for round 2 carrying block N checksum and watermark_seq = 3
        records_n = [
            ("user1::vhost", "user1.org"),
            ("user2::vhost", "user2.org"),
            ("user3::vhost", "user3.org"),
        ]
        crc_n = compute_tree_checksum(records_n)
        now = int(time.time())
        srv_start = len(services.lines)
        services.send_inventory(checksums={'N': (crc_n, now)}, timestamp=now, watermark_seq=3)

        # Follower sees block N checksum mismatch and requests RES 2 N
        print("Waiting for follower RES request for block N...")
        res_line = services.wait_for(lambda l: " DB " in l and " RES " in l and " N" in l,
                                     "Follower RES for block N", start=srv_start)
        round_id = int(res_line.split()[4])
        print(f"PASS: Follower requested RES for block N (round {round_id}).")

        # Step 6: Services provides staged snapshot for block N
        txid = "tx_gap_recovery"
        services.send_begin(round_id, "N", txid, crc_n, 3)
        services.send_put(round_id, "N", txid, "user1::vhost", "user1.org")
        services.send_put(round_id, "N", txid, "user2::vhost", "user2.org")
        services.send_put(round_id, "N", txid, "user3::vhost", "user3.org")
        services.send_end(round_id, "N", txid, crc_n, 3)

        # Step 7: Verify follower committed snapshot, reconciled, and returned to READY / OK
        print("Verifying follower state after snapshot commit...")
        time.sleep(0.5)

        start = len(oper.lines)
        oper.send("DBQ N::user2::vhost")
        oper.wait_for(lambda l: "user2.org" in l, "user2 vhost applied via snapshot", start=start)

        start = len(oper.lines)
        oper.send("DBQ N::user3::vhost")
        oper.wait_for(lambda l: "user3.org" in l, "user3 vhost applied via snapshot", start=start)

        start = len(oper.lines)
        oper.send("UDB STATUS")
        oper.wait_for(lambda l: "Database readiness: READY" in l, "Follower READY after recovery", start=start)
        oper.wait_for(lambda l: "UDB synchronization: OK" in l, "Follower sync OK after recovery", start=start)
        print("PASS: Follower committed staged snapshot, state fully reconciled, status OK.")

        # Step 8: Subsequent in-order sequenced mutation resumes smoothly (seq=4)
        print("Testing subsequent sequenced mutation (seq=4)...")
        start = len(oper.lines)
        services.send("DB * INS 0000000000000001 4 N::user4::vhost user4.org")
        time.sleep(0.3)
        oper.send("DBQ N::user4::vhost")
        oper.wait_for(lambda l: "user4.org" in l, "user4 vhost applied", start=start)
        print("PASS: seq=4 applied successfully. Sequence pipeline fully operational.")

        oper.close()
        services.close()
        print("ALL TESTS PASSED: UDB4 mutation sequencing, gap detection, and self-repair verified.")
        return 0
    finally:
        stop(proc)
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
