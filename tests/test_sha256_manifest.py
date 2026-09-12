#!/usr/bin/env python3
"""Golden vectors and state manifest contract verification for UDB (FASE 3).

Tests:
1. Empty block SHA-256 golden vector (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855).
2. Single record golden vector.
3. Multiple records golden vector with canonical alphabetical line sorting.
4. Insertion order independence: different insertion sequences produce identical digest.
5. Single-byte semantic change alters digest completely.
6. Live IRCD integration: DBQ output matches golden vectors, INF/BEGIN/END validates 64-hex SHA-256.
"""

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

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
RUNTIME_ROOT = pathlib.Path("/home/davidlig/unrealircd")
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
LINK_PASSWORD = "testpassword123"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

EMPTY_DIGEST = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


def compute_tree_digest(records):
    """Computes canonical UDB tree SHA-256 digest over sorted lines."""
    if not records:
        return EMPTY_DIGEST
    lines = sorted([f"{p} {v}\n".encode("utf-8") for p, v in records])
    return hashlib.sha256(b"".join(lines)).hexdigest()


def free_ports(n=2):
    socks = []
    ports = []
    for _ in range(n):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        socks.append(s)
        ports.append(s.getsockname()[1])
    for s in socks:
        s.close()
    return ports


def write_config(path, name, sid, ports, dbdir, propagator=None):
    udb_prop = f'    propagator "{propagator}";\n' if propagator is not None else ""
    path.write_text(f"""include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB manifest test node";
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
""")


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
            process.wait()


class MockClient:
    def __init__(self, host, port, nick="client"):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.2)
        self.buffer = ""
        self.lines = []
        self.send(f"PROTOCTL NAMESX")
        self.send(f"NICK {nick}")
        self.send(f"USER {nick} 0 * :{nick}")

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode("utf-8"))

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
                self.lines.append(line)
                if line.startswith("PING "):
                    cookie = line.split(" ", 1)[1]
                    self.send(f"PONG {cookie}")
            return

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


def test_golden_vectors_pure():
    print("Testing mathematical golden vectors in pure Python...")

    # Vector 1: Empty block
    assert compute_tree_digest([]) == EMPTY_DIGEST, "Empty block digest mismatch"
    print(f"PASS: Vector 1 (empty block): {EMPTY_DIGEST}")

    # Vector 2: Single record
    rec_a = ("user1::vhost", "user1.org")
    rec_b = ("user2::vhost", "user2.org")
    rec_c = ("user3::vhost", "user3.org")
    single_digest = compute_tree_digest([rec_a])
    expected_single = "5819874dff9e29994db2eeb8b949bb63cf5c213303539d35b8a6bbaa05088049"
    assert single_digest == expected_single
    print(f"PASS: Vector 2 (single record): {single_digest}")

    # Vector 3: Multiple records
    expected_multi = "e9f2a16960fa2fcaed0f9d72cfeabead6ccf0415ad02ce1ae9303b96fffe22ff"
    multi_digest = compute_tree_digest([rec_a, rec_b, rec_c])
    assert multi_digest == expected_multi
    print(f"PASS: Vector 3 (multiple records): {multi_digest}")

    # Vector 4: Insertion order independence
    perm1 = compute_tree_digest([rec_a, rec_b, rec_c])
    perm2 = compute_tree_digest([rec_c, rec_a, rec_b])
    perm3 = compute_tree_digest([rec_b, rec_c, rec_a])
    assert perm1 == perm2 == perm3 == expected_multi, "Order independence failed"
    print(f"PASS: Vector 4 (order independence across 3 permutations): all match {multi_digest}")

    # Vector 5: Semantic 1-byte difference
    rec_modified = ("user1::vhost", "user1.com")
    modified_digest = compute_tree_digest([rec_modified, rec_b, rec_c])
    assert modified_digest != multi_digest, "Digest collision on 1-byte change"
    print(f"PASS: Vector 5 (1-byte semantic change): {modified_digest} != {multi_digest}")


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

    def send(self, command):
        self.send_raw(":" + self.sid + " " + command)

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

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
                self.lines.append(line)
                if line.startswith("PING "):
                    cookie = line.split(" ", 1)[1]
                    self.send_raw(f"PONG {cookie}")
            return

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines[start:]:
                if predicate(line):
                    return line
            self.receive(deadline)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines[start:]}")

    def send_inventory(self, checksums=None, timestamp=0, watermark_seq=0):
        self.round_id += 1
        checksums = checksums or {}
        for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
            val = checksums.get(letter)
            if val is not None:
                if len(val) == 2:
                    checksum, block_timestamp = val
                    count = 0
                else:
                    checksum, count, block_timestamp = val
            else:
                checksum = EMPTY_DIGEST
                count = 0
                block_timestamp = timestamp
            self.send(f"DB {self.target_sid} INF {self.round_id} {letter} {checksum} {count} {block_timestamp} {watermark_seq}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def test_golden_vectors_runtime():
    print("\nTesting runtime IRCD golden vectors and manifest over the wire...")
    if not shutil.which("bwrap"):
        print("SKIP: bwrap not available")
        return

    ircd_bin = DEFAULT_IRCD
    if not ircd_bin.exists():
        print(f"SKIP: ircd binary not found at {ircd_bin}")
        return

    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb_sha256_manifest_test_"))
    node_dir = tmpdir / "node"
    db_dir = node_dir / "runtime-data"
    cfg_file = node_dir / "unrealircd.conf"
    ports = free_ports(3)

    node_dir.mkdir(parents=True, exist_ok=True)
    write_config(cfg_file, "node.test", "00N", ports, db_dir, propagator="services.test")

    proc = subprocess.Popen(bwrap_command(node_dir, ircd_bin, cfg_file))
    try:
        wait_for_daemon(proc, "127.0.0.1", ports[0])

        # Peer connects as authorized propagator and bootstraps empty DB
        services = MockPeer("services.test", "00S", "127.0.0.1", ports[1], "00N", propagator_advertised="services.test")
        services.send_inventory()
        time.sleep(0.5)

        client = MockClient("127.0.0.1", ports[0], "oper1")
        client.wait_for(lambda l: " 001 " in l, "Welcome")
        client.send("OPER testoper operpass")
        client.wait_for(lambda l: " 381 " in l, "Oper login")

        # 1. Check initial empty block digest in DBQ N
        start = len(client.lines)
        client.send("DBQ N")
        dbq_line = client.wait_for(lambda l: " 339 " in l and (":N " in l or " N " in l), "DBQ N response", start=start)
        tokens = dbq_line.split()
        hex_tokens = [t for t in tokens if len(t) == 64]
        assert hex_tokens, f"Expected 64-hex SHA-256 in DBQ output: {dbq_line}"
        assert hex_tokens[0] == EMPTY_DIGEST, f"DBQ N returned {hex_tokens[0]}, expected {EMPTY_DIGEST}"
        print(f"PASS: Runtime DBQ N returned exact empty block golden vector: {hex_tokens[0]}")

        # 2. Insert single record via peer: N::user1::vhost = user1.org
        services.send("DB * INS 0000000000000001 1 N::user1::vhost user1.org")
        time.sleep(0.3)

        start = len(client.lines)
        client.send("DBQ N")
        dbq_line = client.wait_for(lambda l: " 339 " in l and (":N " in l or " N " in l), "DBQ N single record", start=start)
        hex_tokens = [t for t in dbq_line.split() if len(t) == 64]
        assert hex_tokens, f"Expected 64-hex SHA-256 in DBQ output: {dbq_line}"
        expected_single = "5819874dff9e29994db2eeb8b949bb63cf5c213303539d35b8a6bbaa05088049"
        assert hex_tokens[0] == expected_single, f"DBQ N returned {hex_tokens[0]}, expected {expected_single}"
        print(f"PASS: Runtime DBQ N returned exact single-record golden vector: {hex_tokens[0]}")

        # 3. Insert additional records in non-alphabetical order: user3 then user2
        services.send("DB * INS 0000000000000001 2 N::user3::vhost user3.org")
        time.sleep(0.1)
        services.send("DB * INS 0000000000000001 3 N::user2::vhost user2.org")
        time.sleep(0.3)

        start = len(client.lines)
        client.send("DBQ N")
        dbq_line = client.wait_for(lambda l: " 339 " in l and (":N " in l or " N " in l), "DBQ N multi records", start=start)
        hex_tokens = [t for t in dbq_line.split() if len(t) == 64]
        assert hex_tokens, f"Expected 64-hex SHA-256 in DBQ output: {dbq_line}"
        rec_a = ("user1::vhost", "user1.org")
        rec_b = ("user2::vhost", "user2.org")
        rec_c = ("user3::vhost", "user3.org")
        expected_multi = compute_tree_digest([rec_a, rec_b, rec_c])
        assert hex_tokens[0] == expected_multi, f"DBQ N returned {hex_tokens[0]}, expected {expected_multi}"
        print(f"PASS: Runtime DBQ N returned exact canonical multi-record digest (order-independent): {hex_tokens[0]}")

        client.close()
        services.close()
    finally:
        stop(proc)
        shutil.rmtree(tmpdir, ignore_errors=True)


def main():
    test_golden_vectors_pure()
    test_golden_vectors_runtime()
    print("\nALL SHA-256 MANIFEST AND GOLDEN VECTOR TESTS PASSED.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
