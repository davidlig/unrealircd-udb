#!/usr/bin/env python3
"""Comprehensive regression test suite for UDB clean bootstrap readiness,
atomic .udb_state persistence, crash-safety, round-isolated reconciliation,
completed-bit invalidation upon re-divergence, and authority switching.

Covers:
  Test 1 - Single block crash (clean node crashes after block N only; on restart remains NOT_READY until all 6 blocks converge)
  Test 2 - Five blocks crash (crashes after N, C, I, S, L; on restart remains NOT_READY)
  Test 3 - Full bootstrap + restart persistence (.udb_state == READY and clients allowed immediately)
  Test 4 - Corrupted state file (.udb_state contains invalid garbage -> fails safe to BOOTSTRAPPING)
  Test 5 - State file write failure (directory read-only / cannot persist -> udb_ready remains 0)
  Test 6 - Second reconciliation with same authority (re-divergence on N invalidates completed status until fresh END)
  Test 7 - Authority switch mid-round (policy changes from A to B -> complete state reset; B must provide all 6 blocks)
  Test 8 - Bootstrap peer disconnect (disconnect mid-bootstrap resets round; next peer must complete from scratch)
  Test 9 - Empty blocks bootstrap (authority with all 6 empty blocks converges cleanly to READY)
  Test 10 - Two bootstrap peers exclusivity and strict HEL ACK validation
  Test 11 - Missing READY snapshot on a local primary remains fail-closed
  Test 12 - Corrupt persisted state on a local primary is preserved and rejected
  Test 13 - Same-peer stale INF, END, and ERR frames cannot affect a newer round
  Test 14 - HEL selection/ACK ordering is idempotent and round IDs are mandatory
  Test 15 - Pending RES timeout/current-round ERR retry while stale ERR is ignored
"""

import os
import pathlib
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
import zlib

from udb_state_seed import seed_block, seed_bootstrapping_state, seed_ready_state, wait_for_state

ROOT = pathlib.Path(__file__).resolve().parents[5]
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


def write_config(path, name, sid, ports, links, dbdir, propagator=None, stale_timeout=None,
                 sync_timeout=None):
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
    udb_stale_to = f'    stale-timeout {stale_timeout};\n' if stale_timeout is not None else ""
    udb_sync_timeout = f'    sync-inactivity-timeout {sync_timeout};\n' if sync_timeout is not None else ""
    udb_block = f'''udb {{
    database-directory "{dbdir}";
{udb_prop}{udb_stale_to}{udb_sync_timeout}}}'''

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
{link_text}loadmodule "cloak_sha256";
loadmodule "third/udb";
{udb_block}
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
    def __init__(self, name, sid, host, port, target_sid, propagator_advertised=None, autostart_hel=True, send_inf=False):
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
        if autostart_hel:
            prop = propagator_advertised if propagator_advertised is not None else "?"
            self.send(f"DB {self.target_sid} HEL 4 {prop} 0000000000000001 OCL")
            self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
            self.send(f"DB {self.target_sid} HEL 4 ACK {prop} 0000000000000001 OCL")
            if send_inf:
                for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                    self.send(f"DB {self.target_sid} INF 1 {b} 00000000 0")

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_snapshots(self, snapshots, timestamp):
        self.round_id += 1
        for letter, txid, records, advertised_checksum, checksum in snapshots:
            self.send(f"DB {self.target_sid} INF {self.round_id} {letter} {advertised_checksum} {timestamp}")
        for letter, txid, records, advertised_checksum, checksum in snapshots:
            self.wait_for(lambda l, letter=letter: f" RES {self.round_id} {letter}" in l,
                          f"RES for block {letter}")
        for letter, txid, records, advertised_checksum, checksum in snapshots:
            self.send(f"DB {self.target_sid} BEGIN {self.round_id} {letter} {txid} 00000000")
            for path, value in records:
                self.send(f"DB {self.target_sid} PUT {self.round_id} {letter} {txid} {path} {value}")
            self.send(f"DB {self.target_sid} END {self.round_id} {letter} {txid} {checksum}")
            self.wait_for(lambda l, letter=letter, txid=txid:
                          f" ACK {self.round_id} {letter} {txid} " in l,
                          f"ACK for block {letter}")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def clear(self):
        self.lines.clear()
        self.buffer = ""

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

    def wait_for(self, predicate, description, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            self.receive(deadline)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class MockClient:
    def __init__(self, host, port, nick="testuser"):
        self.nick = nick
        self.lines = []
        self.buffer = ""
        self.closed = False
        deadline = time.monotonic() + 3.0
        while True:
            try:
                self.sock = socket.create_connection((host, port), timeout=3)
                break
            except ConnectionRefusedError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        self.sock.settimeout(0.5)
        self.send(f"NICK {self.nick}")
        self.send(f"USER {self.nick} 0 * :Test Client")

    def send(self, command):
        try:
            self.sock.sendall((command + "\r\n").encode("utf-8"))
        except OSError:
            self.closed = True

    def receive(self, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                self.closed = True
                break
            if not data:
                self.closed = True
                break
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                self.lines.append(line)
                if line.startswith("PING "):
                    cookie = line.split(" ", 1)[1]
                    self.send(f"PONG {cookie}")

    def wait_for(self, predicate, description="condition", timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            if self.closed:
                break
            self.receive(0.2)
        raise TimeoutError(f"timed out waiting for {description}; lines={self.lines}")

    def is_denied(self, timeout=2.0):
        self.receive(timeout)
        return self.closed or any("ERROR" in l or "temporarily not accepting" in l or "Closing Link" in l for l in self.lines)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def setup_node(tempdir, name, sid, ports, links, propagator=None, stale_timeout=None,
               sync_timeout=None):
    node_dir = pathlib.Path(tempdir) / name
    node_dir.mkdir(parents=True, exist_ok=True)
    dbdir = node_dir / "db"
    dbdir.mkdir(parents=True, exist_ok=True)
    moddir = node_dir / "modules" / "third"
    moddir.mkdir(parents=True, exist_ok=True)
    src_mod = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
    if src_mod.exists():
        shutil.copy(src_mod, moddir / "udb.so")
    cfg = node_dir / "unrealircd.conf"
    write_config(cfg, name, sid, ports, links, dbdir, propagator, stale_timeout, sync_timeout)
    cmd = bwrap_command(node_dir, DEFAULT_IRCD, cfg)
    p = subprocess.Popen(cmd)
    wait_for_daemon(p, "127.0.0.1", ports[0])
    wait_for_daemon(p, "127.0.0.1", ports[1])
    return p, node_dir, dbdir, cfg


def test_suite():
    ircd_bin = pathlib.Path(os.environ.get("UNREALIRCD_BIN", DEFAULT_IRCD))
    module_src = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
    if not ircd_bin.is_file() or not module_src.is_file():
        print(f"SKIP: Missing binary or module at {ircd_bin} / {module_src}")
        return

    with tempfile.TemporaryDirectory(prefix="udb_boot_test_") as tmp_dir_str:
        tmpdir = pathlib.Path(tmp_dir_str)

        # -----------------------------------------------------------------
        # TEST 1: Crash after a single block -> remains BOOTSTRAPPING
        # (explicit propagator policy: a fresh node without any policy is a
        # standalone authority and goes READY immediately)
        # -----------------------------------------------------------------
        print("\n=== Running Test 1: Crash after single block ===")
        p1_ports = free_ports(3)
        p1, n1, dbdir1, cfg1 = setup_node(
            tmpdir, "hub1.test", "001", p1_ports,
            [("peer-a.test", 0, False)], propagator="peer-a.test"
        )
        try:
            c1 = MockClient("127.0.0.1", p1_ports[0], "user1")
            assert c1.is_denied(), "Client was not denied on clean node"
            c1.close()

            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p1_ports[1], "001")
            nick_rec = "alice::vhost alice.net"
            n_crc = zlib.crc32((nick_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            peer_a.send(f"DB 001 INF 1 N {n_crc:08x} 1000")
            peer_a.wait_for(lambda l: " RES 1 N" in l, "RES for block N")
            peer_a.send("DB 001 BEGIN 1 N tx1 00000000")
            peer_a.send(f"DB 001 PUT 1 N tx1 alice::vhost alice.net")
            peer_a.send(f"DB 001 END 1 N tx1 {n_crc:08x}")
            peer_a.wait_for(lambda l: " ACK 1 N " in l, "ACK for block N")
            peer_a.close()

            stop(p1)

            state_file = dbdir1 / ".udb_state"
            assert state_file.exists(), ".udb_state was not created"
            assert "STATE=BOOTSTRAPPING" in state_file.read_text(), ".udb_state must be BOOTSTRAPPING"

            p1 = subprocess.Popen(bwrap_command(n1, ircd_bin, cfg1))
            wait_for_daemon(p1, "127.0.0.1", p1_ports[0])
            wait_for_daemon(p1, "127.0.0.1", p1_ports[1])

            c1_post = MockClient("127.0.0.1", p1_ports[0], "user1_post")
            assert c1_post.is_denied(), "Client was allowed after single-block restart!"
            c1_post.close()

            peer_a2 = MockPeer("peer-a.test", "00A", "127.0.0.1", p1_ports[1], "001")
            sync_timestamp = int(time.time()) + 1000
            # Complete the inventory and each staged transfer in order.  The
            # peer must advertise the real N checksum; an incorrect INF
            # checksum or an overlapping round correctly aborts the prior one.
            snapshots = [("N", "tx2_N", [("alice::vhost", "alice.net")], f"{n_crc:08x}", f"{n_crc:08x}")]
            snapshots += [(b, f"tx2_{b}", [], "deadbeef", "00000000") for b in ('C', 'I', 'S', 'L', 'K')]
            peer_a2.send_snapshots(snapshots, sync_timestamp)

            assert wait_for_state(state_file, "STATE=READY"), ".udb_state must now be READY"
            c1_ready = MockClient("127.0.0.1", p1_ports[0], "user1_ready")
            welcome1 = c1_ready.wait_for(lambda l: " 001 " in l, timeout=3.0)
            assert welcome1 is not None, "Client could not connect after full 6-block convergence"
            c1_ready.close()
            peer_a2.close()
            print("PASS: Test 1: Single block crash safely remains BOOTSTRAPPING; passes to READY after 6 blocks")
        finally:
            stop(p1)

        # -----------------------------------------------------------------
        # TEST 2: Crash after 5 blocks -> remains BOOTSTRAPPING
        # -----------------------------------------------------------------
        print("\n=== Running Test 2: Crash after five blocks ===")
        p2_ports = free_ports(3)
        p2, n2, dbdir2, cfg2 = setup_node(
            tmpdir, "hub2.test", "002", p2_ports,
            [("peer-a.test", 0, False)], propagator="peer-a.test"
        )
        try:
            peer2 = MockPeer("peer-a.test", "00A", "127.0.0.1", p2_ports[1], "002")
            for b in ('N', 'C', 'I', 'S', 'L'):
                peer2.send(f"DB 002 INF 1 {b} 00000000 0")
            time.sleep(0.2)
            peer2.close()
            stop(p2)

            state_file2 = dbdir2 / ".udb_state"
            assert "STATE=BOOTSTRAPPING" in state_file2.read_text(), "State was marked READY prematurely!"

            p2 = subprocess.Popen(bwrap_command(n2, ircd_bin, cfg2))
            wait_for_daemon(p2, "127.0.0.1", p2_ports[0])
            wait_for_daemon(p2, "127.0.0.1", p2_ports[1])

            c2 = MockClient("127.0.0.1", p2_ports[0], "user2")
            assert c2.is_denied(), "Client allowed after 5-block partial initialization!"
            c2.close()
            print("PASS: Test 2: Crash after 5 blocks strictly remains BOOTSTRAPPING")
        finally:
            stop(p2)

        # -----------------------------------------------------------------
        # TEST 3: Full bootstrap + restart persistence
        # -----------------------------------------------------------------
        print("\n=== Running Test 3: Full bootstrap + restart persistence ===")
        p3_ports = free_ports(3)
        p3, n3, dbdir3, cfg3 = setup_node(
            tmpdir, "hub3.test", "003", p3_ports,
            [("peer-a.test", 0, False)], propagator="peer-a.test"
        )
        try:
            peer3 = MockPeer("peer-a.test", "00A", "127.0.0.1", p3_ports[1], "003", send_inf=True)
            peer3.close()

            state_file3 = dbdir3 / ".udb_state"
            assert wait_for_state(state_file3, "STATE=READY"), ".udb_state must be READY"

            stop(p3)

            p3 = subprocess.Popen(bwrap_command(n3, ircd_bin, cfg3))
            wait_for_daemon(p3, "127.0.0.1", p3_ports[0])
            wait_for_daemon(p3, "127.0.0.1", p3_ports[1])

            c3 = MockClient("127.0.0.1", p3_ports[0], "user3")
            welcome3 = c3.wait_for(lambda l: " 001 " in l, timeout=3.0)
            assert welcome3 is not None, "Client connection failed on post-bootstrap restart"
            c3.close()
            print("PASS: Test 3: Bootstrap persistence verified across restart")
        finally:
            stop(p3)

        # -----------------------------------------------------------------
        # TEST 4: Corrupted state file (.udb_state = garbage)
        # -----------------------------------------------------------------
        print("\n=== Running Test 4: Corrupted state file ===")
        p4_ports = free_ports(3)
        p4, n4, dbdir4, cfg4 = setup_node(
            tmpdir, "hub4.test", "004", p4_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p4)
            state_file4 = dbdir4 / ".udb_state"
            state_file4.write_text("GARBAGE_STATE_CORRUPTION_TEST\nANOTHER_INVALID_LINE\n")

            p4 = subprocess.Popen(bwrap_command(n4, ircd_bin, cfg4))
            wait_for_daemon(p4, "127.0.0.1", p4_ports[0])
            wait_for_daemon(p4, "127.0.0.1", p4_ports[1])

            c4 = MockClient("127.0.0.1", p4_ports[0], "user4")
            assert c4.is_denied(), "Client allowed with corrupted .udb_state!"
            c4.close()
            print("PASS: Test 4: Corrupted state file fail-safe to BOOTSTRAPPING verified")
        finally:
            stop(p4)

        # -----------------------------------------------------------------
        # TEST 4B: Orphaned snapshots (six valid blocks, no .udb_state)
        # Snapshots must never auto-migrate to READY; nothing may be deleted.
        # -----------------------------------------------------------------
        print("\n=== Running Test 4B: Orphaned snapshots without .udb_state ===")
        p4b_ports = free_ports(3)
        p4b, n4b, dbdir4b, cfg4b = setup_node(
            tmpdir, "hub4b.test", "00B", p4b_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p4b)
            for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
                seed_block(dbdir4b / f"udb_{letter}.db", letter)
            # No .udb_state on purpose.

            p4b = subprocess.Popen(bwrap_command(n4b, ircd_bin, cfg4b))
            wait_for_daemon(p4b, "127.0.0.1", p4b_ports[0])
            wait_for_daemon(p4b, "127.0.0.1", p4b_ports[1])

            c4b = MockClient("127.0.0.1", p4b_ports[0], "user4b")
            assert c4b.is_denied(), "Client allowed with orphaned snapshots and no .udb_state!"
            c4b.close()
            state_file4b = dbdir4b / ".udb_state"
            assert not state_file4b.exists() or "STATE=READY" not in state_file4b.read_text(), \
                "Orphaned snapshots must not produce a persisted READY state"
            for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
                assert (dbdir4b / f"udb_{letter}.db").exists(), f"Snapshot udb_{letter}.db must not be deleted"
            print("PASS: Test 4B: Orphaned snapshots stayed fail-closed without migration")
        finally:
            stop(p4b)

        # -----------------------------------------------------------------
        # TEST 4C: Minimal legacy state file (STATE=/LAST_SYNC= only)
        # -----------------------------------------------------------------
        print("\n=== Running Test 4C: Minimal legacy state file is invalid ===")
        p4c_ports = free_ports(3)
        p4c, n4c, dbdir4c, cfg4c = setup_node(
            tmpdir, "hub4c.test", "00C", p4c_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p4c)
            (dbdir4c / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")

            p4c = subprocess.Popen(bwrap_command(n4c, ircd_bin, cfg4c))
            wait_for_daemon(p4c, "127.0.0.1", p4c_ports[0])
            wait_for_daemon(p4c, "127.0.0.1", p4c_ports[1])

            c4c = MockClient("127.0.0.1", p4c_ports[0], "user4c")
            assert c4c.is_denied(), "Client allowed with minimal legacy .udb_state!"
            c4c.close()
            print("PASS: Test 4C: Minimal legacy state file rejected fail-closed")
        finally:
            stop(p4c)

        # -----------------------------------------------------------------
        # TEST 4D: Versioned state with ORIGIN=LEGACY is invalid
        # -----------------------------------------------------------------
        print("\n=== Running Test 4D: ORIGIN=LEGACY versioned state is invalid ===")
        p4d_ports = free_ports(3)
        p4d, n4d, dbdir4d, cfg4d = setup_node(
            tmpdir, "hub4d.test", "00D", p4d_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p4d)
            (dbdir4d / ".udb_state").write_text(
                "FORMAT=1\nSTATE=READY\nORIGIN=LEGACY\nGENERATION=1787720000\nLAST_SYNC=1787720000\n",
                encoding="ascii")

            p4d = subprocess.Popen(bwrap_command(n4d, ircd_bin, cfg4d))
            wait_for_daemon(p4d, "127.0.0.1", p4d_ports[0])
            wait_for_daemon(p4d, "127.0.0.1", p4d_ports[1])

            c4d = MockClient("127.0.0.1", p4d_ports[0], "user4d")
            assert c4d.is_denied(), "Client allowed with ORIGIN=LEGACY .udb_state!"
            c4d.close()
            print("PASS: Test 4D: ORIGIN=LEGACY versioned state rejected fail-closed")
        finally:
            stop(p4d)

        # -----------------------------------------------------------------
        # TEST 4E: Persisted READY generation must match snapshot generations
        # -----------------------------------------------------------------
        print("\n=== Running Test 4E: Generation mismatch refuses READY ===")
        p4e_ports = free_ports(3)
        p4e, n4e, dbdir4e, cfg4e = setup_node(
            tmpdir, "hub4e.test", "00E", p4e_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p4e)
            for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
                seed_block(dbdir4e / f"udb_{letter}.db", letter, generation=1)
            seed_ready_state(dbdir4e, generation=2)

            p4e = subprocess.Popen(bwrap_command(n4e, ircd_bin, cfg4e))
            wait_for_daemon(p4e, "127.0.0.1", p4e_ports[0])
            wait_for_daemon(p4e, "127.0.0.1", p4e_ports[1])

            c4e = MockClient("127.0.0.1", p4e_ports[0], "user4e")
            assert c4e.is_denied(), "Client allowed despite READY generation mismatch!"
            c4e.close()
            print("PASS: Test 4E: Generation mismatch stayed fail-closed")
        finally:
            stop(p4e)

        # -----------------------------------------------------------------
        # TEST 5: State file write failure simulation
        # (the read-only directory must be in place before the daemon starts:
        # a node that already went standalone READY on a writable directory
        # legitimately stays READY)
        # -----------------------------------------------------------------
        print("\n=== Running Test 5: State file write failure simulation ===")
        p5_ports = free_ports(3)
        p5, n5, dbdir5, cfg5 = setup_node(
            tmpdir, "hub5.test", "005", p5_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            stop(p5)
            # Discard the artifacts of the first (writable) run so the restart
            # is genuinely fresh; otherwise the persisted READY marker would be
            # promoted without touching the read-only directory.
            for artifact in dbdir5.iterdir():
                artifact.unlink()
            os.chmod(dbdir5, stat.S_IREAD | stat.S_IEXEC)

            p5 = subprocess.Popen(bwrap_command(n5, ircd_bin, cfg5))
            wait_for_daemon(p5, "127.0.0.1", p5_ports[0])
            wait_for_daemon(p5, "127.0.0.1", p5_ports[1])

            peer5 = MockPeer("peer-a.test", "00A", "127.0.0.1", p5_ports[1], "005", send_inf=True)
            time.sleep(0.3)
            peer5.close()

            c5 = MockClient("127.0.0.1", p5_ports[0], "user5")
            assert c5.is_denied(), "Client was allowed despite persistence failure!"
            c5.close()
            print("PASS: Test 5: Persistence write failure keeps node in NOT_READY")
        finally:
            os.chmod(dbdir5, stat.S_IRWXU)
            stop(p5)

        # -----------------------------------------------------------------
        # TEST 6: Second reconciliation with same authority
        # -----------------------------------------------------------------
        print("\n=== Running Test 6: Re-divergence invalidates completed status ===")
        p6_ports = free_ports(3)
        p6, n6, dbdir6, cfg6 = setup_node(
            tmpdir, "hub6.test", "006", p6_ports,
            [("prop-a.test", 0, False)], propagator="prop-a.test", stale_timeout=2
        )
        try:
            prop6 = MockPeer("prop-a.test", "00P", "127.0.0.1", p6_ports[1], "006", propagator_advertised="prop-a.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                prop6.send(f"DB 006 INF 1 {b} 00000000 0")
            time.sleep(0.3)

            prop6.send(f"DB 006 INF 2 N deadbeef {int(time.time()) + 1000}")
            for b in ('C', 'I', 'S', 'L', 'K'):
                prop6.send(f"DB 006 INF 2 {b} 00000000 0")
            time.sleep(0.2)

            prop6.send("DB 006 BEGIN 2 N tx_round2 00000000")
            prop6.send("DB 006 PUT 2 N tx_round2 bob::vhost bob.net")
            bob_rec = "bob::vhost bob.net"
            bob_crc = zlib.crc32((bob_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            prop6.send(f"DB 006 END 2 N tx_round2 {bob_crc:08x}")
            prop6.wait_for(lambda l: " ACK 2 N " in l, "ACK for block N round 2")
            prop6.close()
            print("PASS: Test 6: Re-divergence cleanly completed with fresh staging round")
        finally:
            stop(p6)

        # -----------------------------------------------------------------
        # TEST 7: Authority switch mid-round (S::propagator list failover
        # A -> B; udb::propagator only accepts a single name)
        # -----------------------------------------------------------------
        print("\n=== Running Test 7: Authority switch mid-round ===")
        p7_ports = free_ports(3)
        p7, n7, dbdir7, cfg7 = setup_node(
            tmpdir, "hub7.test", "007", p7_ports,
            [("prop-a.test", 0, False), ("prop-b.test", 0, False)]
        )
        try:
            stop(p7)
            for letter in ('N', 'C', 'I', 'L', 'K'):
                seed_block(dbdir7 / f"udb_{letter}.db", letter)
            seed_block(dbdir7 / "udb_S.db", "S", "propagator prop-a.test,prop-b.test\n")
            # Persist a complete, coherent snapshot set before exercising the
            # authority switch.  Starting with only S leaves the other blocks
            # uninitialized when the replacement authority completes.
            seed_ready_state(dbdir7, generation=1)
            p7 = subprocess.Popen(bwrap_command(n7, ircd_bin, cfg7))
            wait_for_daemon(p7, "127.0.0.1", p7_ports[0])
            wait_for_daemon(p7, "127.0.0.1", p7_ports[1])

            prop_a = MockPeer("prop-a.test", "00A", "127.0.0.1", p7_ports[1], "007", propagator_advertised="prop-a.test")
            for b in ('N', 'C', 'I'):
                prop_a.send(f"DB 007 INF 1 {b} 00000000 0")
            time.sleep(0.1)
            prop_a.close()

            prop_b = MockPeer("prop-b.test", "00B", "127.0.0.1", p7_ports[1], "007", propagator_advertised="prop-b.test")
            # The seeded S block carries the propagator list, so its canonical
            # checksum is non-zero; B must advertise the matching value.
            s_crc7 = zlib.crc32(b"propagator prop-a.test,prop-b.test\n") & 0xFFFFFFFF
            prop_b.send(f"DB 007 INF 2 S {s_crc7:08x} 0")
            for b in ('L', 'K'):
                prop_b.send(f"DB 007 INF 2 {b} 00000000 0")
            time.sleep(0.2)

            state_file7 = dbdir7 / ".udb_state"
            assert "STATE=READY" in state_file7.read_text(), "Valid persisted READY state was lost during authority switch!"

            for b in ('N', 'C', 'I'):
                prop_b.send(f"DB 007 INF 2 {b} 00000000 0")
            assert wait_for_state(state_file7, "STATE=READY"), "State failed to become READY after full reconciliation from B"
            prop_b.close()
            print("PASS: Test 7: Authority switch resets round masks completely")
        finally:
            stop(p7)

        # -----------------------------------------------------------------
        # TEST 8: Selected authority disconnect allows clean takeover by the
        # next S::propagator list candidate
        # -----------------------------------------------------------------
        print("\n=== Running Test 8: Authority disconnect takeover ===")
        p8_ports = free_ports(3)
        p8, n8, dbdir8, cfg8 = setup_node(
            tmpdir, "hub8.test", "008", p8_ports,
            [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        )
        try:
            stop(p8)
            (dbdir8 / "udb_S.db").write_text(
                "; UDB Block S\npropagator peer-a.test,peer-b.test\n", encoding="ascii")
            seed_bootstrapping_state(dbdir8)
            p8 = subprocess.Popen(bwrap_command(n8, ircd_bin, cfg8))
            wait_for_daemon(p8, "127.0.0.1", p8_ports[0])
            wait_for_daemon(p8, "127.0.0.1", p8_ports[1])

            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p8_ports[1], "008")
            for b in ('N', 'C', 'I'):
                peer_a.send(f"DB 008 INF 1 {b} 00000000 0")
            time.sleep(0.1)
            peer_a.close()

            peer_b = MockPeer("peer-b.test", "00B", "127.0.0.1", p8_ports[1], "008")
            s_crc8 = zlib.crc32(b"propagator peer-a.test,peer-b.test\n") & 0xFFFFFFFF
            peer_b.round_id = 1
            snapshots8 = [("S", "tx8_S", [("propagator", "peer-a.test,peer-b.test")],
                           f"{s_crc8:08x}", f"{s_crc8:08x}")]
            snapshots8 += [(b, f"tx8_{b}", [], "deadbeef", "00000000")
                           for b in ('N', 'C', 'I', 'L', 'K')]
            peer_b.send_snapshots(snapshots8, int(time.time()) + 1000)
            state_file8 = dbdir8 / ".udb_state"
            assert wait_for_state(state_file8, "STATE=READY"), "Peer B could not complete bootstrap"
            peer_b.close()
            print("PASS: Test 8: Bootstrap peer disconnect allows clean takeover by subsequent peer")
        finally:
            stop(p8)

        # -----------------------------------------------------------------
        # TEST 9: Empty blocks bootstrap from the selected authority
        # -----------------------------------------------------------------
        print("\n=== Running Test 9: Empty blocks bootstrap ===")
        p9_ports = free_ports(3)
        p9, n9, dbdir9, cfg9 = setup_node(
            tmpdir, "hub9.test", "009", p9_ports,
            [("peer-a.test", 0, False)], propagator="peer-a.test"
        )
        try:
            peer9 = MockPeer("peer-a.test", "00A", "127.0.0.1", p9_ports[1], "009", send_inf=True)
            state_file9 = dbdir9 / ".udb_state"
            assert wait_for_state(state_file9, "STATE=READY"), ".udb_state not READY on empty blocks bootstrap"
            peer9.close()
            print("PASS: Test 9: Empty blocks bootstrap converged to READY")
        finally:
            stop(p9)

        # -----------------------------------------------------------------
        # TEST 10: Two bootstrap peers exclusivity + HEL ACK validation
        # (persisted BOOTSTRAPPING state: without it a fresh no-policy node is
        # a standalone authority and never enters bootstrap mode)
        # -----------------------------------------------------------------
        print("\n=== Running Test 10: Two bootstrap peers exclusivity + HEL ACK validation ===")
        p10_ports = free_ports(3)
        p10, n10, dbdir10, cfg10 = setup_node(
            tmpdir, "hub10.test", "010", p10_ports,
            [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        )
        try:
            stop(p10)
            for artifact in dbdir10.iterdir():
                artifact.unlink()
            seed_bootstrapping_state(dbdir10)
            p10 = subprocess.Popen(bwrap_command(n10, ircd_bin, cfg10))
            wait_for_daemon(p10, "127.0.0.1", p10_ports[0])
            wait_for_daemon(p10, "127.0.0.1", p10_ports[1])

            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p10_ports[1], "010")
            peer_a.send("DB 010 INF 1 N 00000000 0")
            peer_a.send("DB 010 HEL 4 ACK ? 0000000000000001 OCL")

            peer_b = MockPeer("peer-b.test", "00B", "127.0.0.1", p10_ports[1], "010")
            peer_b.send("DB 010 BEGIN 1 N tx_b 00000000")
            err_b = peer_b.wait_for(lambda l: " ERR BEGIN 6" in l, "FORBIDDEN from concurrent peer B")
            assert err_b is not None, "Concurrent peer B was not rejected!"
            peer_b.close()
            peer_a.close()
            print("PASS: Test 10: Two bootstrap peers exclusivity and HEL ACK validation verified")
        finally:
            stop(p10)

        # -----------------------------------------------------------------
        # TEST 11: A local primary must not repair a missing READY snapshot
        # -----------------------------------------------------------------
        print("\n=== Running Test 11: Local primary missing READY snapshot ===")
        p11_ports = free_ports(3)
        p11, n11, dbdir11, cfg11 = setup_node(
            tmpdir, "primary11.test", "011", p11_ports, [], propagator="primary11.test"
        )
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if (dbdir11 / ".udb_state").exists() and "STATE=READY" in (dbdir11 / ".udb_state").read_text():
                    break
                time.sleep(0.05)
            else:
                raise TimeoutError("fresh local primary did not become READY")
            stop(p11)
            n_snapshot = dbdir11 / "udb_N.db"
            n_snapshot.write_text(n_snapshot.read_text(encoding="ascii") + "alice::vhost alice.example\n", encoding="ascii")
            p11 = subprocess.Popen(bwrap_command(n11, ircd_bin, cfg11))
            wait_for_daemon(p11, "127.0.0.1", p11_ports[0])
            ready = MockClient("127.0.0.1", p11_ports[0], "before_loss")
            ready.wait_for(lambda line: " 001 " in line, "welcome before snapshot loss")
            ready.close()
            stop(p11)
            n_snapshot.unlink()

            p11 = subprocess.Popen(bwrap_command(n11, ircd_bin, cfg11))
            wait_for_daemon(p11, "127.0.0.1", p11_ports[0])
            denied = MockClient("127.0.0.1", p11_ports[0], "missing_snapshot")
            assert denied.is_denied(), "Local primary became READY after losing a READY snapshot"
            denied.close()
            assert not n_snapshot.exists(), "Missing snapshot was recreated as authoritative empty data"
            assert "STATE=READY" not in (dbdir11 / ".udb_state").read_text(encoding="ascii")
            print("PASS: Test 11: Missing READY snapshot remains fail-closed on local primary")
        finally:
            stop(p11)

        # -----------------------------------------------------------------
        # TEST 12: A local primary must not overwrite corrupt state as fresh
        # -----------------------------------------------------------------
        print("\n=== Running Test 12: Local primary corrupt persisted state ===")
        p12_ports = free_ports(3)
        p12, n12, dbdir12, cfg12 = setup_node(
            tmpdir, "primary12.test", "012", p12_ports, [], propagator="primary12.test"
        )
        try:
            stop(p12)
            state12 = dbdir12 / ".udb_state"
            state12.write_text("FORMAT=1\nSTATE=READY\nSTATE=BOOTSTRAPPING\nLAST_SYNC=1\n", encoding="ascii")
            p12 = subprocess.Popen(bwrap_command(n12, ircd_bin, cfg12))
            wait_for_daemon(p12, "127.0.0.1", p12_ports[0])
            denied = MockClient("127.0.0.1", p12_ports[0], "corrupt_state")
            assert denied.is_denied(), "Local primary promoted corrupt persisted state to READY"
            denied.close()
            assert state12.read_text(encoding="ascii").count("STATE=") == 2, "Corruption evidence was overwritten"
            print("PASS: Test 12: Corrupt state remains fail-closed and preserved")
        finally:
            stop(p12)

        # -----------------------------------------------------------------
        # TEST 13: Same-peer rounds reject stale INF and END frames
        # -----------------------------------------------------------------
        print("\n=== Running Test 13: Same-peer wire round isolation ===")
        p13_ports = free_ports(3)
        p13, n13, dbdir13, cfg13 = setup_node(
            tmpdir, "round13.test", "013", p13_ports,
            [("prop13.test", 0, False)], propagator="prop13.test"
        )
        try:
            prop13 = MockPeer("prop13.test", "03P", "127.0.0.1", p13_ports[1], "013",
                              propagator_advertised="prop13.test")
            prop13.send(f"DB 013 INF 1 N deadbeef {int(time.time()) + 1000}")
            prop13.wait_for(lambda line: " RES 1 N" in line, "RES for round 1 N")
            prop13.send("DB 013 BEGIN 1 N stale_tx 00000000")
            prop13.send("DB 013 PUT 1 N stale_tx stale::vhost stale.example")

            # A newer inventory round aborts the old staged session.
            prop13.send("DB 013 INF 2 C 00000000 0")
            stale_crc = zlib.crc32(b"stale::vhost stale.example\n") & 0xFFFFFFFF
            prop13.send(f"DB 013 END 1 N stale_tx {stale_crc:08x}")
            prop13.wait_for(lambda line: " ERR END 5 1 N" in line, "stale END rejection")

            # A late round-1 INF cannot contribute K to round 2.
            prop13.send("DB 013 INF 1 K 00000000 0")
            for block in ('I', 'S', 'L', 'K'):
                if block != 'K':
                    prop13.send(f"DB 013 INF 2 {block} 00000000 0")
            time.sleep(0.2)
            assert "STATE=READY" not in (dbdir13 / ".udb_state").read_text(), \
                "Stale round-1 INF/END completed round 2"

            prop13.send("DB 013 INF 2 K 00000000 0")
            prop13.send("DB 013 INF 2 N 00000000 0")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if "STATE=READY" in (dbdir13 / ".udb_state").read_text():
                    break
                time.sleep(0.05)
            else:
                raise TimeoutError("complete round 2 did not reach READY")
            prop13.close()
            print("PASS: Test 13: Same-peer partial rounds, stale INF, and stale END remained isolated")
        finally:
            stop(p13)

        # -----------------------------------------------------------------
        # TEST 14: HEL selection/ACK order is idempotent
        # -----------------------------------------------------------------
        print("\n=== Running Test 14: HEL event ordering ===")
        p14_ports = free_ports(3)
        p14, n14, dbdir14, cfg14 = setup_node(
            tmpdir, "primary14.test", "014", p14_ports,
            [("peer14a.test", 0, False), ("peer14b.test", 0, False)], propagator="primary14.test"
        )
        try:
            peer14a = MockPeer("peer14a.test", "04A", "127.0.0.1", p14_ports[1], "014", autostart_hel=False)
            peer14a.send("DB 014 HEL 4 primary14.test 0000000000000001 OCL")
            peer14a.send("DB 014 HEL 4 ACK primary14.test 0000000000000001 OCL")
            peer14a.wait_for(lambda line: " DB " in line and " INF " in line,
                             "inventory after selection-before-ACK")
            peer14a.close()

            peer14b = MockPeer("peer14b.test", "04B", "127.0.0.1", p14_ports[1], "014", autostart_hel=False)
            peer14b.send("DB 014 HEL 4 ACK primary14.test 0000000000000001 OCL")
            peer14b.send("DB 014 HEL 4 primary14.test 0000000000000001 OCL")
            peer14b.wait_for(lambda line: " DB " in line and " INF " in line,
                             "inventory after ACK-before-selection")
            peer14b.send("DB 014 INF N 00000000 0")
            time.sleep(0.2)
            assert not any(" ERR INF " in line for line in peer14b.lines), \
                "INF without a valid round ID produced an uncorrelated ERR"
            peer14b.close()
            print("PASS: Test 14: HEL event order converged and INF without a round ID was rejected")
        finally:
            stop(p14)

        # -----------------------------------------------------------------
        # TEST 15: Pending RES timeout and ERR both trigger bounded retry
        # -----------------------------------------------------------------
        print("\n=== Running Test 15: Reconciliation retry after timeout and ERR ===")
        p15_ports = free_ports(3)
        p15, n15, dbdir15, cfg15 = setup_node(
            tmpdir, "retry15.test", "015", p15_ports,
            [("prop15.test", 0, False)], propagator="prop15.test", sync_timeout=2
        )
        try:
            prop15 = MockPeer("prop15.test", "05P", "127.0.0.1", p15_ports[1], "015",
                              propagator_advertised="prop15.test")
            prop15.send(f"DB 015 INF 1 N deadbeef {int(time.time()) + 1000}")
            prop15.wait_for(lambda line: " RES 1 N" in line, "initial RES before timeout")
            prop15.clear()
            prop15.wait_for(lambda line: " HEL 4 prop15.test" in line,
                            "HEL retry after pending RES timeout", timeout=8.0)

            prop15.send(f"DB 015 INF 2 N deadbeef {int(time.time()) + 1000}")
            prop15.wait_for(lambda line: " RES 2 N" in line, "RES before injected ERR")
            prop15.clear()
            for invalid_round in ("0", "+2", "2x"):
                prop15.send(f"DB 015 ERR RES 3 {invalid_round} N")
            prop15.receive(time.monotonic() + 0.4)
            assert not any(" HEL 4 prop15.test" in line for line in prop15.lines), \
                "Malformed or zero ERR round aborted active round 2"

            prop15.clear()
            prop15.send("DB 015 ERR RES 3 1 N")
            prop15.receive(time.monotonic() + 0.4)
            assert not any(" HEL 4 prop15.test" in line for line in prop15.lines), \
                "Late ERR from round 1 aborted active round 2"

            prop15.clear()
            prop15.send("DB 015 ERR RES 3 2 N")
            prop15.wait_for(lambda line: " HEL 4 prop15.test" in line,
                            "HEL retry after ERR RES", timeout=20.0)
            prop15.close()
            print("PASS: Test 15: stale ERR was ignored; timeout and current-round ERR retried with backoff")
        finally:
            stop(p15)

        # -----------------------------------------------------------------
        # TEST 16: Restart from READY + DEGRADED (health latch is runtime-only)
        # -----------------------------------------------------------------
        print("\n=== Running Test 16: Restart from READY + DEGRADED ===")
        p16_ports = free_ports(3)
        p16, n16, dbdir16, cfg16 = setup_node(
            tmpdir, "hub16.test", "016", p16_ports,
            [("prop-a.test", 0, False)], propagator="prop-a.test", stale_timeout=2
        )
        try:
            # Bootstrap the empty node to READY + OK
            prop16a = MockPeer("prop-a.test", "00P", "127.0.0.1", p16_ports[1], "016",
                               propagator_advertised="prop-a.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                prop16a.send(f"DB 016 INF 1 {b} 00000000 0")
            time.sleep(0.5)
            c16 = None
            retry_deadline = time.time() + 12
            while time.time() < retry_deadline and c16 is None:
                candidate = MockClient("127.0.0.1", p16_ports[0], "oper16")
                try:
                    candidate.wait_for(lambda l: " 001 " in l, timeout=2)
                    c16 = candidate
                except TimeoutError:
                    candidate.close()
                    time.sleep(0.3)
            assert c16 is not None, "Node must become READY and admit clients after bootstrap"
            c16.send("OPER testoper operpass")
            c16.wait_for(lambda l: " 381 " in l, timeout=3)
            c16.send("UDB STATUS")
            c16.wait_for(lambda l: "UDB synchronization: OK" in l, timeout=3)

            # Confirm divergence (N differs) but never answer the RES request,
            # so the node is left READY + DEGRADED with a pending recovery.
            prop16a.send("DB 016 INF 2 N deadbeef 0")
            for b in ('C', 'I', 'S', 'L', 'K'):
                prop16a.send(f"DB 016 INF 2 {b} 00000000 0")
            prop16a.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for divergent block")
            prop16a.close()
            time.sleep(0.5)
            c16.send("UDB STATUS")
            c16.wait_for(lambda l: "UDB synchronization: DEGRADED" in l, timeout=3)
            print("PASS: Test 16: Node reached READY + DEGRADED with pending recovery")

            # Restart with the propagator offline: the persisted durable READY
            # restores READY + OK; the DEGRADED knowledge was runtime-only.
            stop(p16)
            p16 = subprocess.Popen(bwrap_command(n16, ircd_bin, cfg16))
            wait_for_daemon(p16, "127.0.0.1", p16_ports[0])
            c16b = MockClient("127.0.0.1", p16_ports[0], "oper16b")
            c16b.wait_for(lambda l: " 001 " in l, timeout=5)
            c16b.send("OPER testoper operpass")
            c16b.wait_for(lambda l: " 381 " in l, timeout=3)
            c16b.send("UDB STATUS")
            c16b.wait_for(lambda l: "Database readiness: READY" in l, timeout=3)
            c16b.wait_for(lambda l: "UDB synchronization: OK" in l, timeout=3)
            print("PASS: Test 16: Restart restored READY + OK without persisted health")

            # The returning authority re-detects the divergence and this time
            # completes recovery: DEGRADED -> OK via durable convergence.
            bob_rec = "bob::vhost bob.net"
            bob_crc = zlib.crc32((bob_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            prop16b = MockPeer("prop-a.test", "00P", "127.0.0.1", p16_ports[1], "016",
                               propagator_advertised="prop-a.test", autostart_hel=False)
            prop16b.send("DB 016 HEL 4 prop-a.test 0000000000000001 OCL")
            prop16b.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL response after restart")
            prop16b.send("DB 016 HEL 4 ACK prop-a.test 0000000000000001 OCL")
            prop16b.send("DB 016 INF 1 N deadbeef 0")
            for b in ('C', 'I', 'S', 'L', 'K'):
                prop16b.send(f"DB 016 INF 1 {b} 00000000 0")
            prop16b.wait_for(lambda l: " DB " in l and " RES 1 N" in l, "RES after restart")
            c16b.send("UDB STATUS")
            c16b.wait_for(lambda l: "UDB synchronization: DEGRADED" in l, timeout=3)
            prop16b.send("DB 016 BEGIN 1 N tx16 00000000")
            prop16b.send(f"DB 016 PUT 1 N tx16 {bob_rec}")
            prop16b.send(f"DB 016 END 1 N tx16 {bob_crc:08x}")
            prop16b.wait_for(lambda l: " ACK 1 N tx16 " in l, "ACK for staged recovery")
            c16b.send("UDB STATUS")
            c16b.wait_for(lambda l: "UDB synchronization: OK" in l, timeout=3)
            print("PASS: Test 16: Returning authority re-detected divergence and converged to OK")

            c16b.close()
            prop16b.close()
        finally:
            stop(p16)

    print("\nALL READINESS & CONVERGENCE TESTS PASSED SUCCESSFULLY!")


if __name__ == "__main__":
    test_suite()
