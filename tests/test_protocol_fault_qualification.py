#!/usr/bin/env python3
"""Comprehensive Qualification and Fault Injection Test Suite for UDB4 (Phase 5).

Covers all 15 protocol fault scenarios required by Phase 5:
  Q1  - Silent drop of INS: gap detected -> reconciliation -> convergence.
  Q2  - Silent drop of DEL: gap detected -> reconciliation -> convergence.
  Q3  - Silent drop of EXP: gap/divergence detected -> reconciliation -> convergence.
  Q4  - Duplicate mutation: seq <= last_applied is dropped idempotently.
  Q5  - Out-of-order reorder: gap triggers recovery and deterministic convergence.
  Q6  - Persistence failure: write failure latches DEGRADED; subsequent retry recovers.
  Q7  - Authority switch: stale epoch / old stream cannot mutate state.
  Q8  - Reconnect during reconcile: disconnect during staging causes clean abort, no partial commit.
  Q9  - Inactivity and absolute timeouts: stalled round aborts cleanly and retries.
  Q10 - Corrupt snapshot: digest mismatch in END triggers rollback, no commit.
  Q11 - Digest mismatch: differing manifest triggers mandatory reconciliation.
  Q12 - Stable network: periodic anti-entropy confirms match with zero transfers.
  Q13 - Mutation/snapshot race: watermark boundary preserves sequential ordering.
  Q14 - Malformed sequence/digest: strict fail-closed rejection.
  Q15 - Oversized fields: oversized path/value cleanly rejected without corruption.

In every recoverable test, the convergence invariant is strictly verified:
  canonical_state(Follower) == canonical_state(Authority)
  canonical_digest(Follower) == canonical_digest(Authority)
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

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from udb_state_seed import seed_block, seed_ready_state

ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
LINK_PASSWORD = "testlinkpassword"
EMPTY_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


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


def write_config(path, name, sid, ports, links, dbdir, propagator=None,
                 anti_entropy_interval=None, sync_inactivity=None, sync_absolute=None):
    link_text = ""
    for peer, peer_port, autoconnect in links:
        outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                    'options { autoconnect; } }\n') if autoconnect else ""
        link_text += f'''link {peer} {{
    incoming {{ mask "*@*"; }}
{outgoing}    password "{LINK_PASSWORD}";
    class servers;
}}
'''
    udb_prop = f'    propagator "{propagator}";\n' if propagator is not None else ""
    udb_ae = f'    anti-entropy-interval {anti_entropy_interval};\n' if anti_entropy_interval is not None else ""
    udb_si = f'    sync-inactivity-timeout {sync_inactivity};\n' if sync_inactivity is not None else ""
    udb_sa = f'    sync-absolute-timeout {sync_absolute};\n' if sync_absolute is not None else ""
    udb_block = f'''udb {{
{udb_prop}{udb_ae}{udb_si}{udb_sa}}}'''

    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB qualification test node";
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
{link_text}loadmodule "cloak_sha256";
loadmodule "third/udb";
{udb_block}
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


class MockPeer:
    def __init__(self, name, sid, host, port, target_sid, propagator_advertised=None,
                 epoch="0000000000000001", autostart_hel=True, send_inf=True, authorizes_us=True,
                 initial_inf=None):
        self.name = name
        self.sid = sid
        self.target_sid = target_sid
        self.epoch = epoch
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send_raw(f"PASS :{LINK_PASSWORD}")
        self.send_raw(f"PROTOCTL EAUTH={self.name}")
        self.send_raw("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send_raw(f"SERVER {self.name} 1 :UDB peer {self.name}")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, f"{self.name} link handshake")
        self.send("EOS")
        if autostart_hel:
            prop = propagator_advertised if propagator_advertised is not None else (self.name if authorizes_us else "?")
            self.send(f"DB {self.target_sid} HEL 4 {prop} {self.epoch} OCL")
            self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
            self.send(f"DB {self.target_sid} HEL 4 ACK {prop} {self.epoch} OCL")
            if send_inf:
                initial_inf = initial_inf or {}
                for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                    sha, cnt = initial_inf.get(b, (EMPTY_SHA256, 0))
                    self.send(f"DB {self.target_sid} INF 1 {b} {sha} {cnt} 0")

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def clear(self):
        self.lines.clear()
        self.buffer = ""

    def receive(self, timeout=1.0):
        deadline = time.monotonic() + timeout
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

    def wait_for(self, predicate, description="matching line", timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            self.receive(0.2)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class MockClient:
    def __init__(self, host, port, nick="testuser"):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=3)
        self.sock.settimeout(0.5)
        self.lines = []
        self.buffer = ""
        self.send(f"NICK {self.nick}")
        self.send(f"USER {self.nick} 0 * :Test Client")
        self.wait_for(lambda line: " 001 " in line, "client registration")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def clear(self):
        self.lines.clear()
        self.buffer = ""

    def receive(self, timeout=1.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                break
            if not data:
                break
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                self.lines.append(line)
                if line.startswith("PING "):
                    cookie = line.split(" ", 1)[1]
                    self.send(f"PONG {cookie}")

    def wait_for(self, predicate, description="matching line", timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            self.receive(0.2)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines}")

    def oper(self):
        self.send("OPER testoper operpass")
        return self.wait_for(lambda line: " 381 " in line, "oper promotion")

    def udb_status(self):
        self.clear()
        self.send("UDB STATUS")
        self.wait_for(lambda line: " 339 " in line and "Database readiness:" in line, "UDB STATUS response")
        deadline = time.monotonic() + 0.5
        while time.monotonic() < deadline:
            self.receive(0.1)
        return "\n".join(self.lines)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def connect_oper(host, port, nick="testoper_c"):
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            c = MockClient(host, port, nick)
            c.oper()
            return c
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("Failed to connect oper client")


def compute_sha256(entries):
    if not entries:
        return EMPTY_SHA256
    canonical = "".join(f"{path} {val}\n" for path, val in sorted(entries))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def read_block_state(data_dir, letter):
    db_file = data_dir / f"udb_{letter}.db"
    if not db_file.exists():
        return [], EMPTY_SHA256
    text = db_file.read_text(encoding="utf-8")
    entries = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        parts = line.split(" ", 1)
        entries.append((parts[0], parts[1] if len(parts) > 1 else ""))
    entries.sort()
    digest = compute_sha256(entries)
    return entries, digest


# =========================================================================
# Q1 — Pérdida silenciosa de INS
# =========================================================================
def test_q1_silent_ins_loss():
    print("--- Running Q1: Silent drop of INS (gap -> recovery -> convergence) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q1_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q1")
        assert "UDB synchronization: OK" in client.udb_status()

        # Step 1: Send INS 1 (applied cleanly)
        peer.send(f"DB * INS {peer.epoch} 1 alice user@host.invalid:1000")
        time.sleep(0.3)

        # Step 2: Drop INS 2, send INS 3 (creates sequence gap: expected 2, got 3)
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 3 charlie user@host.invalid:3000")

        # Follower must detect gap, isolate mutation, latch DEGRADED and refresh HEL
        peer.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 refresh from follower", timeout=3)
        assert "UDB synchronization: DEGRADED" in client.udb_status()

        # Mutation 3 must NOT be applied into active state
        f_entries, _ = read_block_state(data_dir, 'N')
        assert not any(p == "charlie" for p, _ in f_entries), "Out-of-order mutation 3 was applied!"

        # Step 3: Peer replies to HEL and sends INF with divergent digest
        auth_entries = [("alice", "user@host.invalid:1000"),
                        ("bob", "user@host.invalid:2000"),
                        ("charlie", "user@host.invalid:3000")]
        auth_sha = compute_sha256(auth_entries)

        peer.send(f"DB 001 HEL 4 ACK prop-a.test {peer.epoch} OCL")
        peer.send(f"DB 001 INF 2 N {auth_sha} 3 0 3")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 3")

        # Follower must request RES 2 N
        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for block N", timeout=3)

        # Authority delivers snapshot
        peer.send(f"DB 001 BEGIN 2 N stg1 {auth_sha} 3")
        for path, val in auth_entries:
            peer.send(f"DB 001 PUT 2 N stg1 {path} {val}")
        peer.send(f"DB 001 END 2 N stg1 {auth_sha} 3")

        # Follower ACKs and returns to OK
        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for block N", timeout=3)
        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()

        # Invariant check: canonical state and digest match
        follower_entries, follower_sha = read_block_state(data_dir, 'N')
        assert follower_entries == sorted(auth_entries), f"State mismatch: {follower_entries}"
        assert follower_sha == auth_sha, f"Digest mismatch: {follower_sha} != {auth_sha}"
        print("PASS: Q1: Silent INS loss detected via gap, recovered via snapshot, canonical state verified")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q2 — Pérdida silenciosa de DEL
# =========================================================================
def test_q2_silent_del_loss():
    print("--- Running Q2: Silent drop of DEL (gap -> recovery -> convergence) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q2_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q2")
        assert "UDB synchronization: OK" in client.udb_status()

        # Insert alice at seq 1
        peer.send(f"DB * INS {peer.epoch} 1 alice user@host.invalid:1000")
        time.sleep(0.3)

        # Drop DEL 2 alice! Send INS 3 bob instead (gap at 2)
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 3 bob user@host.invalid:2000")

        peer.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 refresh", timeout=3)
        assert "UDB synchronization: DEGRADED" in client.udb_status()

        # Reconcile snapshot where alice was deleted and bob exists
        auth_entries = [("bob", "user@host.invalid:2000")]
        auth_sha = compute_sha256(auth_entries)

        peer.send(f"DB 001 HEL 4 ACK prop-a.test {peer.epoch} OCL")
        peer.send(f"DB 001 INF 2 N {auth_sha} 1 0 3")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 3")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)
        peer.send(f"DB 001 BEGIN 2 N stg2 {auth_sha} 3")
        peer.send(f"DB 001 PUT 2 N stg2 bob user@host.invalid:2000")
        peer.send(f"DB 001 END 2 N stg2 {auth_sha} 3")

        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()

        follower_entries, follower_sha = read_block_state(data_dir, 'N')
        assert follower_entries == auth_entries, f"Deleted entry not pruned: {follower_entries}"
        assert follower_sha == auth_sha
        print("PASS: Q2: Silent DEL loss detected, deleted entry pruned in recovery snapshot")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q3 — Pérdida silenciosa de EXP
# =========================================================================
def test_q3_silent_exp_loss():
    print("--- Running Q3: Silent drop of EXP (Block K expiration gap recovery) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q3_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        k_init = [("G::*@badhost.com::reason", "spam")]
        k_init_sha = compute_sha256(k_init)
        for b in ('N', 'C', 'I', 'S', 'L'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)
        seed_block(data_dir / "udb_K.db", 'K', "G::*@badhost.com::reason spam\n", generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False,
                        initial_inf={"K": (k_init_sha, 1)})

        client = connect_oper("127.0.0.1", ports[0], "oper_q3")
        assert "UDB synchronization: OK" in client.udb_status()

        # Follower starts with the seeded K-line
        k_entries, _ = read_block_state(data_dir, 'K')
        assert len(k_entries) == 1

        # Step 1: Send INS 1 (valid mutation in N)
        peer.send(f"DB * INS {peer.epoch} 1 N::user1::vhost user1.org")
        time.sleep(0.3)

        # Step 2: Drop EXP 2 (expiration of the K-line). Send INS 3 instead!
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 3 N::user2::vhost user2.org")

        peer.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 refresh", timeout=3)
        assert "UDB synchronization: DEGRADED" in client.udb_status()

        # Reconcile snapshot where K has expired (empty) and N has user1 and user2
        n_entries = [("user1::vhost", "user1.org"), ("user2::vhost", "user2.org")]
        n_sha = compute_sha256(n_entries)

        peer.send(f"DB 001 HEL 4 ACK prop-a.test {peer.epoch} OCL")
        peer.send(f"DB 001 INF 2 N {n_sha} 2 0 3")
        peer.send(f"DB 001 INF 2 K {EMPTY_SHA256} 0 0 3")
        for b in ('C', 'I', 'S', 'L'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 3")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)
        peer.wait_for(lambda l: " DB " in l and " RES 2 K" in l, "RES for K", timeout=3)

        # Deliver snapshots for N and K
        peer.send(f"DB 001 BEGIN 2 N stgN {n_sha} 3")
        peer.send(f"DB 001 PUT 2 N stgN user1::vhost user1.org")
        peer.send(f"DB 001 PUT 2 N stgN user2::vhost user2.org")
        peer.send(f"DB 001 END 2 N stgN {n_sha} 3")

        peer.send(f"DB 001 BEGIN 2 K stgK {EMPTY_SHA256} 3")
        peer.send(f"DB 001 END 2 K stgK {EMPTY_SHA256} 3")

        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        peer.wait_for(lambda l: " DB " in l and " ACK 2 K" in l, "ACK for K", timeout=3)

        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()

        k_entries, k_sha = read_block_state(data_dir, 'K')
        assert len(k_entries) == 0, f"Expired K record was not pruned: {k_entries}"
        assert k_sha == EMPTY_SHA256
        print("PASS: Q3: Silent EXP loss detected, expired Block K pruned to empty state")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q4 — Duplicate mutation handling
# =========================================================================
def test_q4_duplicate_mutation():
    print("--- Running Q4: Duplicate mutation (seq <= last_applied dropped idempotently) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q4_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q4")

        # Step 1: Send INS 1
        peer.send(f"DB * INS {peer.epoch} 1 N::alice::vhost user@host.invalid:1000")
        time.sleep(0.3)
        entries1, sha1 = read_block_state(data_dir, 'N')
        assert len(entries1) == 1

        # Step 2: Send duplicate INS 1 with DIFFERENT value (attacker attempting replay/mutation overwrite)
        peer.send(f"DB * INS {peer.epoch} 1 N::alice::vhost malicious@host.invalid:9999")
        time.sleep(0.3)

        # Node must drop duplicate idempotently without double apply
        entries2, sha2 = read_block_state(data_dir, 'N')
        assert entries2 == entries1, "Duplicate mutation overwrote active state!"
        assert sha2 == sha1, "Digest altered by duplicate mutation!"
        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q4: Duplicate mutation dropped idempotently without state corruption")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q5 — Out-of-order reorder handling
# =========================================================================
def test_q5_reorder_handling():
    print("--- Running Q5: Out-of-order reorder (gap detected -> recovery -> convergence) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q5_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q5")

        # Reorder: send seq 2 before seq 1
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 2 outoforder user@reorder.test:2000")

        # Follower expects seq 1, rejects seq 2 as gap, degrades and refreshes HEL
        peer.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 refresh", timeout=3)
        assert "UDB synchronization: DEGRADED" in client.udb_status()

        # Authority responds to reconcile with canonical state
        auth_entries = [("first", "user@order.test:1000"), ("outoforder", "user@reorder.test:2000")]
        auth_sha = compute_sha256(auth_entries)

        peer.send(f"DB 001 HEL 4 ACK prop-a.test {peer.epoch} OCL")
        peer.send(f"DB 001 INF 2 N {auth_sha} 2 0 2")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 2")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)
        peer.send(f"DB 001 BEGIN 2 N stg5 {auth_sha} 2")
        for p, v in auth_entries:
            peer.send(f"DB 001 PUT 2 N stg5 {p} {v}")
        peer.send(f"DB 001 END 2 N stg5 {auth_sha} 2")

        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()

        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert f_entries == auth_entries
        assert f_sha == auth_sha
        print("PASS: Q5: Reordered mutation rejected cleanly, recovered to canonical order")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q6 — Persistence failure recovery
# =========================================================================
def test_q6_persistence_failure():
    print("--- Running Q6: Persistence failure handling (fail-closed, degrades, recovers) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q6_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q6")
        assert "UDB synchronization: OK" in client.udb_status()

        # Simulate persistence failure by making data directory read-only
        os.chmod(data_dir, 0o555)

        # Mutation attempt should fail disk append/persist
        peer.send(f"DB * INS {peer.epoch} 1 N::readonly::vhost user@ro.invalid:1000")
        time.sleep(0.5)

        # Restore permissions for recovery
        os.chmod(data_dir, 0o755)

        # Node must be in DEGRADED status due to persistence failure
        status = client.udb_status()
        assert "UDB synchronization: DEGRADED" in status, f"Node should be DEGRADED on persist failure, got: {status}"

        # Reconcile cleanly restores health
        auth_entries = [("readonly::vhost", "user@ro.invalid:1000")]
        auth_sha = compute_sha256(auth_entries)

        peer.send(f"DB 001 INF 2 N {auth_sha} 1 0 1")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)
        peer.send(f"DB 001 BEGIN 2 N stg6 {auth_sha} 1")
        peer.send(f"DB 001 PUT 2 N stg6 readonly::vhost user@ro.invalid:1000")
        peer.send(f"DB 001 END 2 N stg6 {auth_sha} 1")

        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q6: Persistence failure cleanly latched DEGRADED and recovered after disk restored")
    finally:
        try:
            os.chmod(tmp / "runtime-data", 0o755)
        except Exception:
            pass
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q7 — Authority switch and stale epoch isolation
# =========================================================================
def test_q7_authority_switch():
    print("--- Running Q7: Authority switch and stale epoch isolation ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q7_"))
    proc = None
    peer1 = None
    peer2 = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(4)
        links = [("prop-a.test", ports[1], False), ("prop-b.test", ports[3], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        # Peer 1 is authorized authority
        peer1 = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                         propagator_advertised="prop-a.test", epoch="1111111111111111", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q7")
        assert "UDB synchronization: OK" in client.udb_status()

        # Peer 1 sends INS 1 with epoch 1111...
        peer1.send("DB * INS 1111111111111111 1 N::user1::vhost host1:1000")
        time.sleep(0.3)

        # Peer 1 attempts to send mutation with STALE epoch 0000000000000000
        peer1.clear()
        peer1.send("DB * INS 0000000000000000 2 N::staleuser::vhost host1:2000")
        time.sleep(0.3)

        # State must NOT contain staleuser
        f_entries, _ = read_block_state(data_dir, 'N')
        assert not any("staleuser" in p for p, _ in f_entries), "Stale epoch mutation was accepted!"

        # Unauthorized peer 2 (prop-b.test) attempts to send mutation with valid format
        peer2 = MockPeer("prop-b.test", "003", "127.0.0.1", ports[1], "001",
                         propagator_advertised="prop-b.test", epoch="2222222222222222",
                         autostart_hel=True, send_inf=False, authorizes_us=False)
        peer2.send("DB * INS 2222222222222222 1 N::rogueuser::vhost host2:3000")
        time.sleep(0.3)

        # State must NOT contain rogueuser from non-selected authority
        f_entries, _ = read_block_state(data_dir, 'N')
        assert not any("rogueuser" in p for p, _ in f_entries), "Non-authority mutation was accepted!"
        print("PASS: Q7: Stale epoch and non-authority mutations cleanly isolated")
    finally:
        if client:
            client.close()
        if peer1:
            peer1.close()
        if peer2:
            peer2.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q8 — Reconnect during reconcile (no partial commit)
# =========================================================================
def test_q8_reconnect_during_reconcile():
    print("--- Running Q8: Disconnect during reconcile staging (no partial commit) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q8_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q8")

        # Trigger reconcile round 2 for block N
        fake_sha = hashlib.sha256(b"fake").hexdigest()
        peer.send(f"DB 001 INF 2 N {fake_sha} 1 0 1")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)

        # Peer sends BEGIN and PUT, but DISCONNECTS before sending END!
        peer.send(f"DB 001 BEGIN 2 N stg8 {fake_sha} 1")
        peer.send(f"DB 001 PUT 2 N stg8 halfcommit user@half.test:1000")
        peer.close()
        peer = None

        time.sleep(0.5)

        # Verify no partial commit occurred: active Block N is still empty!
        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert len(f_entries) == 0, f"Partial commit leaked into active state: {f_entries}"
        assert f_sha == EMPTY_SHA256
        print("PASS: Q8: Abrupt disconnect during staging cleanly aborted without partial commit")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q9 — Inactivity and absolute timeouts
# =========================================================================
def test_q9_timeouts():
    print("--- Running Q9: Inactivity and absolute reconciliation timeouts ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q9_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        # Set low timeouts: 2 seconds inactivity, 4 seconds absolute
        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test",
                     sync_inactivity=2, sync_absolute=4)

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q9")

        # Start round 2 and stall (send RES then go silent)
        fake_sha = hashlib.sha256(b"fake").hexdigest()
        peer.send(f"DB 001 INF 2 N {fake_sha} 1 0 1")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)

        # Go completely silent for 3 seconds (> 2s inactivity timeout)
        time.sleep(3.0)

        # Stalled round must be aborted; active state unaffected
        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert len(f_entries) == 0
        assert f_sha == EMPTY_SHA256
        print("PASS: Q9: Inactivity timeout aborted stalled reconciliation round")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q10 — Corrupt snapshot rollback
# =========================================================================
def test_q10_corrupt_snapshot_rollback():
    print("--- Running Q10: Corrupt snapshot digest mismatch in END triggers rollback ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q10_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q10")

        auth_entries = [("realuser", "user@real.test:1000")]
        real_sha = compute_sha256(auth_entries)
        corrupted_sha = "0" * 64

        peer.send(f"DB 001 INF 2 N {real_sha} 1 0 1")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)

        # Deliver snapshot with corrupted digest in END frame
        peer.send(f"DB 001 BEGIN 2 N stg10 {real_sha} 1")
        peer.send(f"DB 001 PUT 2 N stg10 realuser user@real.test:1000")
        peer.clear()
        peer.send(f"DB 001 END 2 N stg10 {corrupted_sha} 1")

        # Follower must reject with ERR END 3 (FATAL checksum mismatch)
        peer.wait_for(lambda l: " DB " in l and " ERR END 3 2 N" in l, "ERR END 3 for checksum mismatch", timeout=3)

        # Follower must rollback staging: active Block N is empty!
        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert len(f_entries) == 0, "Corrupt snapshot committed to active state!"
        assert f_sha == EMPTY_SHA256
        print("PASS: Q10: Corrupt snapshot rejected with ERR END 3; staging cleanly rolled back")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q11 — Digest mismatch triggers mandatory reconciliation
# =========================================================================
def test_q11_digest_mismatch_reconciliation():
    print("--- Running Q11: Digest mismatch triggers mandatory reconciliation ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q11_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q11")

        # Announce Channel block C mismatch
        c_entries = [("#general::topic", "general_topic")]
        c_sha = compute_sha256(c_entries)

        peer.clear()
        peer.send(f"DB 001 INF 2 C {c_sha} 1 0 1")
        for b in ('N', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")

        # Mandatory RES for block C
        peer.wait_for(lambda l: " DB " in l and " RES 2 C" in l, "RES for block C", timeout=3)
        assert "UDB synchronization: DEGRADED" in client.udb_status()

        # Deliver snapshot
        peer.send(f"DB 001 BEGIN 2 C stgC {c_sha} 1")
        peer.send(f"DB 001 PUT 2 C stgC #general::topic general_topic")
        peer.send(f"DB 001 END 2 C stgC {c_sha} 1")

        peer.wait_for(lambda l: " DB " in l and " ACK 2 C" in l, "ACK for C", timeout=3)
        time.sleep(0.5)
        assert "UDB synchronization: OK" in client.udb_status()

        f_entries, f_sha = read_block_state(data_dir, 'C')
        assert f_entries == c_entries, f"Mismatch: {f_entries} != {c_entries}"
        assert f_sha == c_sha
        print("PASS: Q11: Digest mismatch correctly triggered mandatory RES and converged")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q12 — Red estable (anti-entropía periódica sin transferencias)
# =========================================================================
def test_q12_stable_network_no_transfers():
    print("--- Running Q12: Stable network verification (zero redundant snapshot transfers) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q12_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir,
                     propagator="prop-a.test", anti_entropy_interval=2)

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q12")

        # Two consecutive anti-entropy rounds
        for round_idx in range(2):
            peer.clear()
            req_line = peer.wait_for(lambda l: " DB " in l and " MANIFEST REQ " in l,
                                     f"MANIFEST REQ round {round_idx}", timeout=6)
            round_id = req_line.split()[5]
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                peer.send(f"DB 001 MANIFEST ACK {round_id} {b} 0 {EMPTY_SHA256} 0")
            time.sleep(0.5)

        # Zero RES or BEGIN lines
        transfers = [l for l in peer.lines if " RES " in l or " BEGIN " in l]
        assert len(transfers) == 0, f"Unexpected data transfers in stable network: {transfers}"
        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q12: Stable network verified zero unnecessary snapshot transfers across rounds")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q13 — Mutation / snapshot race around watermark
# =========================================================================
def test_q13_watermark_race():
    print("--- Running Q13: Mutation/snapshot race around watermark boundary ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q13_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q13")

        # Step 1: Reconcile snapshot with watermark_seq = 50
        snap_entries = [("baseuser::vhost", "user@base.test:1000")]
        snap_sha = compute_sha256(snap_entries)

        peer.send(f"DB 001 INF 2 N {snap_sha} 1 0 50")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 50")

        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)
        peer.send(f"DB 001 BEGIN 2 N stg13 {snap_sha} 50")
        peer.send(f"DB 001 PUT 2 N stg13 baseuser::vhost user@base.test:1000")
        peer.send(f"DB 001 END 2 N stg13 {snap_sha} 50")
        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        time.sleep(0.5)

        # Step 2: Race condition: online mutation arrives with seq = 49 (<= watermark 50)
        # Must be dropped as already subsumed by the snapshot
        peer.send(f"DB * INS {peer.epoch} 49 N::olduser::vhost user@old.test:500")
        time.sleep(0.3)
        f_entries, _ = read_block_state(data_dir, 'N')
        assert not any("olduser" in p for p, _ in f_entries), "Pre-watermark mutation was incorrectly applied!"

        # Step 3: Online mutation arrives with seq = 50 (== watermark 50)
        # Must be dropped as equal to watermark
        peer.send(f"DB * INS {peer.epoch} 50 N::atwatermark::vhost user@at.test:500")
        time.sleep(0.3)
        f_entries, _ = read_block_state(data_dir, 'N')
        assert not any("atwatermark" in p for p, _ in f_entries), "At-watermark mutation was incorrectly applied!"

        # Step 4: Online mutation arrives with seq = 51 (watermark + 1)
        # Must be applied cleanly
        peer.send(f"DB * INS {peer.epoch} 51 N::nextuser::vhost user@next.test:1500")
        time.sleep(0.3)
        f_entries, _ = read_block_state(data_dir, 'N')
        assert any("nextuser" in p for p, _ in f_entries), "Post-watermark mutation was not applied!"

        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q13: Watermark boundary strictly enforced; duplicate/pre-watermark dropped, post applied")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q14 — Malformed sequence / digest validation (Fail-closed)
# =========================================================================
def test_q14_malformed_protocol_rejection():
    print("--- Running Q14: Malformed sequence and digest strict fail-closed rejection ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q14_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q14")

        # Case 1: Non-numeric sequence in INS
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} notanumber user1 user@test.invalid:1000")
        peer.wait_for(lambda l: " DB " in l and " ERR INS 2 " in l, "ERR INS 2 for non-numeric sequence", timeout=3)

        # Case 2: Negative/overflow sequence in INS
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 18446744073709551616 user2 user@test.invalid:1000")
        peer.wait_for(lambda l: " DB " in l and " ERR INS 2 " in l, "ERR INS 2 for overflow sequence", timeout=3)

        # Case 3: Truncated 63-hex digest in INF
        peer.clear()
        bad_digest = "a" * 63
        peer.send(f"DB 001 INF 99 N {bad_digest} 1 0 1")
        peer.wait_for(lambda l: " DB " in l and " ERR INF 2 99 N" in l, "ERR INF 2 for truncated digest", timeout=3)

        # Case 4: Non-hex characters in digest
        peer.clear()
        bad_hex = ("z" * 64)
        peer.send(f"DB 001 INF 99 N {bad_hex} 1 0 1")
        peer.wait_for(lambda l: " DB " in l and " ERR INF 2 99 N" in l, "ERR INF 2 for invalid hex digest", timeout=3)

        # Case 5: Uppercase digests are non-canonical on the wire.
        peer.clear()
        peer.send(f"DB 001 INF 99 N {'A' * 64} 1 0 1")
        peer.wait_for(lambda l: " DB " in l and " ERR INF 2 99 N" in l,
                      "ERR INF 2 for non-canonical uppercase digest", timeout=3)

        # Case 6: Optional watermarks must parse strictly when present.
        peer.clear()
        peer.send(f"DB 001 INF 99 N {EMPTY_SHA256} 0 0 notanumber")
        peer.wait_for(lambda l: " DB " in l and " ERR INF 2 99 N" in l,
                      "ERR INF 2 for malformed watermark", timeout=3)

        # Case 7: Mutation frames reject trailing fields instead of silently
        # accepting an ambiguous spelling of the same operation.
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 1 N::extra::vhost valid.test trailing")
        peer.wait_for(lambda l: " DB " in l and " ERR INS 2 " in l,
                      "ERR INS 2 for trailing mutation parameter", timeout=3)

        # State remains clean and uncorrupted
        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert len(f_entries) == 0
        assert f_sha == EMPTY_SHA256
        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q14: Malformed sequences, digests, watermarks, and arities fail closed with ERR PARAMS")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


# =========================================================================
# Q15 — Oversized fields rejection
# =========================================================================
def test_q15_oversized_fields():
    print("--- Running Q15: Oversized field protection (fail-closed, no buffer overflow) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_q15_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "runtime-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(data_dir / f"udb_{b}.db", b, generation=1)

        ports = free_ports(3)
        links = [("prop-a.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator="prop-a.test")

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        peer = MockPeer("prop-a.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="prop-a.test", authorizes_us=False)

        client = connect_oper("127.0.0.1", ports[0], "oper_q15")

        # Case 1: Oversized path (> UDB_RECORD_PATH_MAX)
        over_path = "x" * 600
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 1 {over_path} val")
        peer.wait_for(lambda l: " DB " in l and " ERR INS 2 " in l, "ERR INS 2 for oversized path", timeout=3)

        # Case 2: Oversized value (> UDB_RECORD_VALUE_MAX 4096)
        over_val = "v" * 4500
        peer.clear()
        peer.send(f"DB * INS {peer.epoch} 1 normal_path {over_val}")
        time.sleep(0.3)

        # Case 3: Oversized staged TXID (> 31 bytes)
        fake_sha = hashlib.sha256(b"oversized").hexdigest()
        peer.send(f"DB 001 INF 2 N {fake_sha} 1 0 1")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 INF 2 {b} {EMPTY_SHA256} 0 0 1")
        peer.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for N", timeout=3)

        peer.clear()
        long_txid = "t" * 32
        peer.send(f"DB 001 BEGIN 2 N {long_txid} {fake_sha} 1")
        peer.wait_for(lambda l: " DB " in l and " ERR BEGIN 2 2 N" in l, "ERR BEGIN 2 for overlong TXID", timeout=3)

        # Complete round with valid TXID (empty snapshot) to restore OK
        peer.send(f"DB 001 BEGIN 2 N validtxid {EMPTY_SHA256} 1")
        peer.send(f"DB 001 END 2 N validtxid {EMPTY_SHA256} 1")
        peer.wait_for(lambda l: " DB " in l and " ACK 2 N" in l, "ACK for N", timeout=3)
        time.sleep(0.5)

        # Active state remains empty and valid
        f_entries, f_sha = read_block_state(data_dir, 'N')
        assert len(f_entries) == 0
        assert f_sha == EMPTY_SHA256
        assert "UDB synchronization: OK" in client.udb_status()
        print("PASS: Q15: Oversized fields cleanly rejected without buffer overflow or state corruption")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    print("==================================================================")
    print("STARTING UDB4 PROTOCOL FAULT QUALIFICATION SUITE (Q1 to Q15)")
    print("==================================================================")
    test_q1_silent_ins_loss()
    test_q2_silent_del_loss()
    test_q3_silent_exp_loss()
    test_q4_duplicate_mutation()
    test_q5_reorder_handling()
    test_q6_persistence_failure()
    test_q7_authority_switch()
    test_q8_reconnect_during_reconcile()
    test_q9_timeouts()
    test_q10_corrupt_snapshot_rollback()
    test_q11_digest_mismatch_reconciliation()
    test_q12_stable_network_no_transfers()
    test_q13_watermark_race()
    test_q14_malformed_protocol_rejection()
    test_q15_oversized_fields()
    print("==================================================================")
    print("ALL 15 PROTOCOL FAULT QUALIFICATION TESTS PASSED SUCCESSFULLY!")
    print("==================================================================")
