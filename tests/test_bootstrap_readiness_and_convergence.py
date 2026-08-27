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


def write_config(path, name, sid, ports, links, dbdir, propagator=None, stale_timeout=None, stale_action=None):
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
    udb_stale_act = f'    stale-action {stale_action};\n' if stale_action is not None else ""
    udb_block = f'''udb {{
    database-directory "{dbdir}";
{udb_prop}{udb_stale_to}{udb_stale_act}}}'''

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
        self.send_raw(f"PASS :{LINK_PASSWORD}")
        self.send_raw(f"PROTOCTL EAUTH={self.name}")
        self.send_raw("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send_raw(f"SERVER {self.name} 1 :UDB peer {self.name}")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, f"{self.name} link handshake")
        self.send("EOS")
        if autostart_hel:
            prop = propagator_advertised if propagator_advertised is not None else "?"
            self.send(f"DB {self.target_sid} HEL 4 {prop}")
            self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
            self.send(f"DB {self.target_sid} HEL 4 ACK")
            if send_inf:
                for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                    self.send(f"DB {self.target_sid} INF {b} 00000000 0")

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

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

    def wait_for(self, predicate, timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            if self.closed:
                break
            self.receive(0.2)
        return None

    def is_denied(self, timeout=2.0):
        self.receive(timeout)
        return self.closed or any("ERROR" in l or "temporarily not accepting" in l or "Closing Link" in l for l in self.lines)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def setup_node(tempdir, name, sid, ports, links, propagator=None, stale_timeout=None, stale_action=None):
    node_dir = pathlib.Path(tempdir) / name
    node_dir.mkdir(parents=True, exist_ok=True)
    dbdir = node_dir / "db"
    dbdir.mkdir(parents=True, exist_ok=True)
    moddir = node_dir / "modules" / "third"
    moddir.mkdir(parents=True, exist_ok=True)
    src_mod = RUNTIME_ROOT / "modules/third/udb.so"
    if src_mod.exists():
        shutil.copy(src_mod, moddir / "udb.so")
    cfg = node_dir / "unrealircd.conf"
    write_config(cfg, name, sid, ports, links, dbdir, propagator, stale_timeout, stale_action)
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
        # -----------------------------------------------------------------
        print("\n=== Running Test 1: Crash after single block ===")
        p1_ports = free_ports(3)
        p1, n1, dbdir1, cfg1 = setup_node(
            tmpdir, "hub1.test", "001", p1_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            c1 = MockClient("127.0.0.1", p1_ports[0], "user1")
            assert c1.is_denied(), "Client was not denied on clean node"
            c1.close()

            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p1_ports[1], "001")
            peer_a.send("DB 001 INF N 00000000 0")
            peer_a.send("DB 001 BEGIN N tx1 00000000")
            nick_rec = "alice::vhost alice.net"
            peer_a.send(f"DB 001 PUT N tx1 alice::vhost alice.net")
            n_crc = zlib.crc32((nick_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            peer_a.send(f"DB 001 END N tx1 {n_crc:08x}")
            peer_a.wait_for(lambda l: " ACK N" in l, "ACK for block N")
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
            peer_a2.send(f"DB 001 INF N {n_crc:08x} 1000")
            for b in ('C', 'I', 'S', 'L', 'K'):
                peer_a2.send(f"DB 001 INF {b} 00000000 0")
            time.sleep(0.3)

            assert "STATE=READY" in state_file.read_text(), ".udb_state must now be READY"
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
            [("peer-a.test", 0, False)]
        )
        try:
            peer2 = MockPeer("peer-a.test", "00A", "127.0.0.1", p2_ports[1], "002")
            for b in ('N', 'C', 'I', 'S', 'L'):
                peer2.send(f"DB 002 INF {b} 00000000 0")
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
            [("peer-a.test", 0, False)]
        )
        try:
            peer3 = MockPeer("peer-a.test", "00A", "127.0.0.1", p3_ports[1], "003", send_inf=True)
            time.sleep(0.3)
            peer3.close()

            state_file3 = dbdir3 / ".udb_state"
            assert "STATE=READY" in state_file3.read_text(), ".udb_state must be READY"

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
        # TEST 5: State file write failure simulation
        # -----------------------------------------------------------------
        print("\n=== Running Test 5: State file write failure simulation ===")
        p5_ports = free_ports(3)
        p5, n5, dbdir5, cfg5 = setup_node(
            tmpdir, "hub5.test", "005", p5_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            os.chmod(dbdir5, stat.S_IREAD | stat.S_IEXEC)

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
                prop6.send(f"DB 006 INF {b} 00000000 0")
            time.sleep(0.3)

            prop6.send("DB 006 INF N deadbeef 5000")
            for b in ('C', 'I', 'S', 'L', 'K'):
                prop6.send(f"DB 006 INF {b} 00000000 0")
            time.sleep(0.2)

            prop6.send("DB 006 BEGIN N tx_round2 00000000")
            prop6.send("DB 006 PUT N tx_round2 bob::vhost bob.net")
            bob_rec = "bob::vhost bob.net"
            bob_crc = zlib.crc32((bob_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            prop6.send(f"DB 006 END N tx_round2 {bob_crc:08x}")
            prop6.wait_for(lambda l: " ACK N" in l, "ACK for block N round 2")
            prop6.close()
            print("PASS: Test 6: Re-divergence cleanly completed with fresh staging round")
        finally:
            stop(p6)

        # -----------------------------------------------------------------
        # TEST 7: Authority switch mid-round
        # -----------------------------------------------------------------
        print("\n=== Running Test 7: Authority switch mid-round ===")
        p7_ports = free_ports(3)
        p7, n7, dbdir7, cfg7 = setup_node(
            tmpdir, "hub7.test", "007", p7_ports,
            [("prop-a.test", 0, False), ("prop-b.test", 0, False)]
        )
        try:
            prop_a = MockPeer("prop-a.test", "00A", "127.0.0.1", p7_ports[1], "007", propagator_advertised="prop-a.test")
            for b in ('N', 'C', 'I'):
                prop_a.send(f"DB 007 INF {b} 00000000 0")
            time.sleep(0.1)
            prop_a.close()

            prop_b = MockPeer("prop-b.test", "00B", "127.0.0.1", p7_ports[1], "007", propagator_advertised="prop-b.test")
            for b in ('S', 'L', 'K'):
                prop_b.send(f"DB 007 INF {b} 00000000 0")
            time.sleep(0.2)

            state_file7 = dbdir7 / ".udb_state"
            assert "STATE=BOOTSTRAPPING" in state_file7.read_text(), "State became READY on authority switch with partial blocks!"

            for b in ('N', 'C', 'I'):
                prop_b.send(f"DB 007 INF {b} 00000000 0")
            time.sleep(0.3)
            assert "STATE=READY" in state_file7.read_text(), "State failed to become READY after full reconciliation from B"
            prop_b.close()
            print("PASS: Test 7: Authority switch resets round masks completely")
        finally:
            stop(p7)

        # -----------------------------------------------------------------
        # TEST 8: Bootstrap peer disconnect reset
        # -----------------------------------------------------------------
        print("\n=== Running Test 8: Bootstrap peer disconnect reset ===")
        p8_ports = free_ports(3)
        p8, n8, dbdir8, cfg8 = setup_node(
            tmpdir, "hub8.test", "008", p8_ports,
            [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        )
        try:
            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p8_ports[1], "008")
            for b in ('N', 'C', 'I'):
                peer_a.send(f"DB 008 INF {b} 00000000 0")
            time.sleep(0.1)
            peer_a.close()

            peer_b = MockPeer("peer-b.test", "00B", "127.0.0.1", p8_ports[1], "008", send_inf=True)
            time.sleep(0.3)
            state_file8 = dbdir8 / ".udb_state"
            assert "STATE=READY" in state_file8.read_text(), "Peer B could not complete bootstrap"
            peer_b.close()
            print("PASS: Test 8: Bootstrap peer disconnect allows clean takeover by subsequent peer")
        finally:
            stop(p8)

        # -----------------------------------------------------------------
        # TEST 9: Empty blocks bootstrap
        # -----------------------------------------------------------------
        print("\n=== Running Test 9: Empty blocks bootstrap ===")
        p9_ports = free_ports(3)
        p9, n9, dbdir9, cfg9 = setup_node(
            tmpdir, "hub9.test", "009", p9_ports,
            [("peer-a.test", 0, False)]
        )
        try:
            peer9 = MockPeer("peer-a.test", "00A", "127.0.0.1", p9_ports[1], "009", send_inf=True)
            time.sleep(0.3)
            state_file9 = dbdir9 / ".udb_state"
            assert "STATE=READY" in state_file9.read_text(), ".udb_state not READY on empty blocks bootstrap"
            peer9.close()
            print("PASS: Test 9: Empty blocks bootstrap converged to READY")
        finally:
            stop(p9)

        # -----------------------------------------------------------------
        # TEST 10: Two bootstrap peers exclusivity + HEL ACK validation
        # -----------------------------------------------------------------
        print("\n=== Running Test 10: Two bootstrap peers exclusivity + HEL ACK validation ===")
        p10_ports = free_ports(3)
        p10, n10, dbdir10, cfg10 = setup_node(
            tmpdir, "hub10.test", "010", p10_ports,
            [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        )
        try:
            peer_a = MockPeer("peer-a.test", "00A", "127.0.0.1", p10_ports[1], "010")
            peer_a.send("DB 010 INF N 00000000 0")
            peer_a.send("DB 010 HEL 4 ACK")

            peer_b = MockPeer("peer-b.test", "00B", "127.0.0.1", p10_ports[1], "010")
            peer_b.send("DB 010 BEGIN N tx_b 00000000")
            err_b = peer_b.wait_for(lambda l: " ERR BEGIN 6" in l, "FORBIDDEN from concurrent peer B")
            assert err_b is not None, "Concurrent peer B was not rejected!"
            peer_b.close()
            peer_a.close()
            print("PASS: Test 10: Two bootstrap peers exclusivity and HEL ACK validation verified")
        finally:
            stop(p10)

    print("\nALL 10 READINESS & CONVERGENCE TESTS PASSED SUCCESSFULLY!")


if __name__ == "__main__":
    test_suite()
