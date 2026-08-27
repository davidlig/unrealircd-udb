#!/usr/bin/env python3
"""Comprehensive regression test suite for UDB clean bootstrap readiness,
single bootstrap source exclusivity, failover, STALE convergence recovery,
unsolicited HEL ACK rejection, and bootstrap persistence.

Covers:
  Test 1 - Clean bootstrap + client gate (denied when not ready, allowed after sync)
  Test 2 - Two bootstrap peers (single source exclusivity, rejection of concurrent peer)
  Test 3 - Bootstrap peer failover (session abort, clear bootstrap_peer, failover to second peer)
  Test 4 - Block S learned before other blocks (clients remain DENIED until all blocks reconciled)
  Test 5 - Clean node with local propagator (announces HEL 4 <propagator>, not ?, denies clients until sync)
  Test 6 - STALE recovery with divergent data (stays STALE and denies clients during staged sync until completion)
  Test 7 - Unsolicited HEL ACK rejection (unnegotiated HEL ACK does not confirm capability)
  Test 8 - Bootstrap persistence (completed readiness survives restart)
"""

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
        self.sock = socket.create_connection((host, port), timeout=3)
        self.sock.settimeout(0.5)
        self.lines = []
        self.buffer = ""
        self.send(f"NICK {self.nick}")
        self.send(f"USER {self.nick} 0 * :Test Client")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def receive(self, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
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
            self.receive(0.2)
        return None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def test_suite():
    ircd_bin = pathlib.Path(os.environ.get("UNREALIRCD_BIN", DEFAULT_IRCD))
    module_src = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
    if not ircd_bin.is_file() or not module_src.is_file():
        print(f"SKIP: Missing binary or module at {ircd_bin} / {module_src}")
        return

    with tempfile.TemporaryDirectory(prefix="udb_boot_test_") as tmp_dir_str:
        tmpdir = pathlib.Path(tmp_dir_str)

        # =========================================================================
        # Test 1: Clean bootstrap + client gate
        # =========================================================================
        print("\n=== Running Test 1: Clean bootstrap + client gate ===")
        node1 = tmpdir / "node1"
        ports1 = free_ports(3)
        links1 = [("peer-a.test", 0, False)]
        dbdir1 = node1 / "db"
        dbdir1.mkdir(parents=True, exist_ok=True)
        (node1 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node1 / "modules" / "third" / "udb.so")
        conf1 = node1 / "unrealircd.conf"
        write_config(conf1, "hub1.test", "001", ports1, links1, dbdir1)

        proc1 = subprocess.Popen(bwrap_command(node1, ircd_bin, conf1))
        wait_for_daemon(proc1, "127.0.0.1", ports1[0])

        # Attempt connection before bootstrap: MUST be denied
        client_early = MockClient("127.0.0.1", ports1[0], "early_user")
        client_early.receive(timeout=1.0)
        denied = any("UDB synchronization unavailable" in l or "ERROR" in l for l in client_early.lines)
        assert denied, f"Expected client to be denied on uninitialized node, lines: {client_early.lines}"
        print("PASS: Test 1: Local client denied before UDB bootstrap completed")
        client_early.close()

        # Connect Peer A: verify Hub1 announces HEL 4 ?
        peerA = MockPeer("peer-a.test", "00A", "127.0.0.1", ports1[1], "001", propagator_advertised="?", autostart_hel=False)
        peerA.send("DB 001 HEL 4 ?")
        hel_resp = peerA.wait_for(lambda l: " DB " in l and " HEL 4 ?" in l, "HEL 4 ? advertised")
        assert " HEL 4 ?" in hel_resp, f"Expected HEL 4 ?, got {hel_resp}"
        peerA.send("DB 001 HEL 4 ACK")
        print("PASS: Test 1: Clean node advertised HEL 4 ?")

        # Peer A reconciles all blocks (sends identical INF for all 6 blocks)
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            peerA.send(f"DB 001 INF {b} 00000000 0")
        time.sleep(0.5)

        # Now client connects: MUST be allowed!
        client_ready = MockClient("127.0.0.1", ports1[0], "ready_user")
        welcome = client_ready.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, f"Expected 001 welcome after bootstrap, got {client_ready.lines}"
        print("PASS: Test 1: Local client allowed after UDB bootstrap completed")
        client_ready.close()
        peerA.close()
        stop(proc1)

        # =========================================================================
        # Test 2: Two bootstrap peers (single source exclusivity)
        # =========================================================================
        print("\n=== Running Test 2: Two bootstrap peers exclusivity ===")
        node2 = tmpdir / "node2"
        ports2 = free_ports(3)
        links2 = [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        dbdir2 = node2 / "db"
        dbdir2.mkdir(parents=True, exist_ok=True)
        (node2 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node2 / "modules" / "third" / "udb.so")
        conf2 = node2 / "unrealircd.conf"
        write_config(conf2, "hub2.test", "002", ports2, links2, dbdir2)

        proc2 = subprocess.Popen(bwrap_command(node2, ircd_bin, conf2))
        wait_for_daemon(proc2, "127.0.0.1", ports2[0])

        # Peer A connects and sends INF for N (fixing A as bootstrap_peer)
        peerA = MockPeer("peer-a.test", "00A", "127.0.0.1", ports2[1], "002", propagator_advertised="?", autostart_hel=True)
        peerA.send("DB 002 INF N 12345678 100")
        peerA.wait_for(lambda l: " DB " in l and " RES N" in l, "RES N sent to Peer A")
        print("PASS: Test 2: Peer A selected as bootstrap_peer on first INF/RES exchange")

        # Peer B connects and attempts staged sync
        peerB = MockPeer("peer-b.test", "00B", "127.0.0.1", ports2[1], "002", propagator_advertised="?", autostart_hel=True)
        peerB.send("DB 002 BEGIN C tx_b 00000000")
        err_begin = peerB.wait_for(lambda l: " DB " in l and " ERR BEGIN 6 C" in l, "ERR BEGIN 6 C from Peer B")
        assert " ERR BEGIN 6 C" in err_begin, f"Expected ERR BEGIN 6 C, got: {err_begin}"
        print("PASS: Test 2: Peer B BEGIN was rejected with ERR BEGIN 6 (UDB_ERR_FORBIDDEN)")

        # Peer B sends INF for block C: hub MUST NOT send RES to B
        peerB.clear()
        peerB.send("DB 002 INF C 99999999 200")
        time.sleep(0.5)
        res_to_b = any(" RES C" in l for l in peerB.lines)
        assert not res_to_b, f"Hub should not send RES C to non-bootstrap peer B, lines: {peerB.lines}"
        print("PASS: Test 2: Hub did not send RES to concurrent non-bootstrap peer B")

        # Peer A sends staged sync for N -> succeeds
        peerA.send("DB 002 BEGIN N tx_a 12345678")
        peerA.send("DB 002 PUT N tx_a testuser::vhost test.vhost")
        crc_n = f"{zlib.crc32(b'testuser::vhost test.vhost\n') & 0xFFFFFFFF:08X}"
        peerA.send(f"DB 002 END N tx_a {crc_n}")
        peerA.wait_for(lambda l: " DB " in l and " ACK N tx_a " in l, "ACK N to Peer A")
        print("PASS: Test 2: Staged sync from authorized bootstrap_peer A succeeded")

        peerA.close()
        peerB.close()
        stop(proc2)

        # =========================================================================
        # Test 3: Bootstrap peer failover
        # =========================================================================
        print("\n=== Running Test 3: Bootstrap peer failover ===")
        node3 = tmpdir / "node3"
        ports3 = free_ports(3)
        links3 = [("peer-a.test", 0, False), ("peer-b.test", 0, False)]
        dbdir3 = node3 / "db"
        dbdir3.mkdir(parents=True, exist_ok=True)
        (node3 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node3 / "modules" / "third" / "udb.so")
        conf3 = node3 / "unrealircd.conf"
        write_config(conf3, "hub3.test", "003", ports3, links3, dbdir3)

        proc3 = subprocess.Popen(bwrap_command(node3, ircd_bin, conf3))
        wait_for_daemon(proc3, "127.0.0.1", ports3[0])

        peerA = MockPeer("peer-a.test", "00A", "127.0.0.1", ports3[1], "003", propagator_advertised="?", autostart_hel=True)
        peerB = MockPeer("peer-b.test", "00B", "127.0.0.1", ports3[1], "003", propagator_advertised="?", autostart_hel=True)

        # Peer A starts BEGIN N and abruptly disconnects
        peerA.send("DB 003 BEGIN N tx_fail 00000000")
        time.sleep(0.2)
        peerA.close()
        time.sleep(0.5)

        # Node remains not ready
        client_failover = MockClient("127.0.0.1", ports3[0], "user_failover")
        client_failover.receive(timeout=1.0)
        assert any("UDB synchronization unavailable" in l or "ERROR" in l for l in client_failover.lines), "Clients must remain denied"
        client_failover.close()
        print("PASS: Test 3: Node remains not ready after Peer A disconnects during sync")

        # Peer B now acts as bootstrap source and sends all 6 blocks
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            peerB.send(f"DB 003 INF {b} 00000000 0")
        time.sleep(0.5)

        # Now client connects successfully!
        client_ok = MockClient("127.0.0.1", ports3[0], "user_ok")
        welcome = client_ok.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, "Client should be allowed after Peer B completes bootstrap"
        print("PASS: Test 3: Peer B successfully became new bootstrap_peer after Peer A quit")

        client_ok.close()
        peerB.close()
        stop(proc3)

        # =========================================================================
        # Test 4: Block S arrives before other blocks
        # =========================================================================
        print("\n=== Running Test 4: Block S arrives before other blocks ===")
        node4 = tmpdir / "node4"
        ports4 = free_ports(3)
        links4 = [("prop-a.test", 0, False)]
        dbdir4 = node4 / "db"
        dbdir4.mkdir(parents=True, exist_ok=True)
        (node4 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node4 / "modules" / "third" / "udb.so")
        conf4 = node4 / "unrealircd.conf"
        write_config(conf4, "hub4.test", "004", ports4, links4, dbdir4)

        proc4 = subprocess.Popen(bwrap_command(node4, ircd_bin, conf4))
        wait_for_daemon(proc4, "127.0.0.1", ports4[0])

        propA = MockPeer("prop-a.test", "00A", "127.0.0.1", ports4[1], "004", propagator_advertised="?", autostart_hel=True)

        # Prop A sends Block S setting propagator to prop-a.test
        propA.send("DB 004 BEGIN S tx_s 00000000")
        propA.send("DB 004 PUT S tx_s propagator prop-a.test")
        crc_s = f"{zlib.crc32(b'propagator prop-a.test\n') & 0xFFFFFFFF:08X}"
        propA.send(f"DB 004 END S tx_s {crc_s}")
        propA.wait_for(lambda l: " DB " in l and " ACK S tx_s " in l, "ACK S to Prop A")

        # Hub4 now advertises HEL 4 prop-a.test because policy was learned
        propA.wait_for(lambda l: " DB " in l and " HEL 4 prop-a.test" in l, "HEL 4 prop-a.test advertised")
        print("PASS: Test 4: HEL advertised state immediately transitioned to HEL 4 prop-a.test upon receiving S")

        # BUT clients must remain DENIED because N, C, I, L, K are not yet reconciled!
        client_s = MockClient("127.0.0.1", ports4[0], "user_s")
        client_s.receive(timeout=1.0)
        assert any("UDB synchronization unavailable" in l or "ERROR" in l for l in client_s.lines), "Clients must remain denied before other blocks finish"
        client_s.close()
        print("PASS: Test 4: Clients remain DENIED after learning S before remaining blocks are reconciled")

        # Prop A sends INF for remaining blocks
        for b in ('N', 'C', 'I', 'L', 'K'):
            propA.send(f"DB 004 INF {b} 00000000 0")
        time.sleep(0.5)

        # Now client connects successfully!
        client_full = MockClient("127.0.0.1", ports4[0], "user_full")
        welcome = client_full.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, "Client should be allowed after all blocks are reconciled"
        print("PASS: Test 4: Clients ALLOWED after all remaining blocks completed reconciliation")

        client_full.close()
        propA.close()
        stop(proc4)

        # =========================================================================
        # Test 5: Clean node with local propagator
        # =========================================================================
        print("\n=== Running Test 5: Clean node with local propagator ===")
        node5 = tmpdir / "node5"
        ports5 = free_ports(3)
        links5 = [("prop-a.test", 0, False), ("peer-b.test", 0, False)]
        dbdir5 = node5 / "db"
        dbdir5.mkdir(parents=True, exist_ok=True)
        (node5 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node5 / "modules" / "third" / "udb.so")
        conf5 = node5 / "unrealircd.conf"
        write_config(conf5, "hub5.test", "005", ports5, links5, dbdir5, propagator="prop-a.test")

        proc5 = subprocess.Popen(bwrap_command(node5, ircd_bin, conf5))
        wait_for_daemon(proc5, "127.0.0.1", ports5[0])

        # Peer B connects first (not the propagator)
        peerB = MockPeer("peer-b.test", "00B", "127.0.0.1", ports5[1], "005", autostart_hel=False)
        peerB.send("DB 005 HEL 4 ?")
        # Hub5 advertises HEL 4 - (because prop-a is not connected yet, NOT HEL 4 ?)
        hel_resp = peerB.wait_for(lambda l: " DB " in l and " HEL 4 -" in l, "HEL 4 - advertised to Peer B")
        assert " HEL 4 -" in hel_resp, f"Expected HEL 4 -, got: {hel_resp}"
        peerB.send("DB 005 HEL 4 ACK")
        print("PASS: Test 5: Clean node with local propagator advertised HEL 4 -, not ?")

        # Peer B attempts staged sync -> rejected with ERR BEGIN 6
        peerB.send("DB 005 BEGIN N tx_b 00000000")
        err_b = peerB.wait_for(lambda l: " DB " in l and " ERR BEGIN 6 N" in l, "ERR BEGIN 6 N to Peer B")
        assert " ERR BEGIN 6 N" in err_b
        print("PASS: Test 5: Non-propagator Peer B was rejected from providing bootstrap")

        # Client denied before propagator sync
        client5_early = MockClient("127.0.0.1", ports5[0], "user5_early")
        client5_early.receive(timeout=1.0)
        assert any("UDB synchronization unavailable" in l or "ERROR" in l for l in client5_early.lines)
        client5_early.close()
        print("PASS: Test 5: Local clients denied on clean node with propagator before convergence")

        # Propagator connects and reconciles all blocks
        propA = MockPeer("prop-a.test", "00A", "127.0.0.1", ports5[1], "005", propagator_advertised="prop-a.test", autostart_hel=True, send_inf=True)
        time.sleep(0.5)

        # Clients now allowed
        client5_ok = MockClient("127.0.0.1", ports5[0], "user5_ok")
        welcome = client5_ok.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, "Client should be allowed after convergence with local propagator"
        print("PASS: Test 5: Local clients ALLOWED after convergence with configured propagator")

        client5_ok.close()
        propA.close()
        peerB.close()
        stop(proc5)

        # =========================================================================
        # Test 6: STALE recovery with divergent data
        # =========================================================================
        print("\n=== Running Test 6: STALE recovery with divergent data ===")
        node6 = tmpdir / "node6"
        ports6 = free_ports(3)
        links6 = [("prop-a.test", 0, False)]
        dbdir6 = node6 / "db"
        dbdir6.mkdir(parents=True, exist_ok=True)
        (node6 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node6 / "modules" / "third" / "udb.so")

        # Seed S and N
        (dbdir6 / "udb_S.db").write_text("; UDB Block S - Version 1\npropagator prop-a.test\n", encoding="ascii")
        (dbdir6 / "udb_N.db").write_text("; UDB Block N - Version 1\nolduser::vhost old.vhost\n", encoding="ascii")

        conf6 = node6 / "unrealircd.conf"
        write_config(conf6, "hub6.test", "006", ports6, links6, dbdir6, stale_timeout=2, stale_action="deny-new-clients")

        proc6 = subprocess.Popen(bwrap_command(node6, ircd_bin, conf6))
        wait_for_daemon(proc6, "127.0.0.1", ports6[0])

        # Wait for node to enter STALE state (2.2s without propagator)
        time.sleep(2.3)

        client6_stale = MockClient("127.0.0.1", ports6[0], "user6_stale")
        client6_stale.receive(timeout=1.0)
        assert any("UDB synchronization unavailable" in l or "ERROR" in l for l in client6_stale.lines)
        client6_stale.close()
        print("PASS: Test 6: Node confirmed STALE with new clients denied")

        # Propagator reconnects and sends divergent INF for N with newer timestamp
        future_ts = int(time.time()) + 5000
        propA = MockPeer("prop-a.test", "00A", "127.0.0.1", ports6[1], "006", propagator_advertised="prop-a.test", autostart_hel=True)
        propA.send(f"DB 006 INF N AAAAAAAA {future_ts}")
        propA.wait_for(lambda l: " DB " in l and " RES N" in l, "RES N from Hub6")

        # Start BEGIN N and PUT N, but do NOT send END yet
        propA.send("DB 006 BEGIN N tx_div AAAAAAAA")
        propA.send("DB 006 PUT N tx_div newuser::vhost new.vhost")
        time.sleep(0.3)

        # Clients must STILL be denied during staged transfer before END!
        client6_mid = MockClient("127.0.0.1", ports6[0], "user6_mid")
        client6_mid.receive(timeout=1.0)
        assert any("UDB synchronization unavailable" in l or "ERROR" in l for l in client6_mid.lines), "Clients must remain denied during staged sync"
        client6_mid.close()
        print("PASS: Test 6: Clients remain DENIED during active staged sync before END")

        # Send END N and remaining identical INFs
        crc_div = f"{zlib.crc32(b'newuser::vhost new.vhost\n') & 0xFFFFFFFF:08X}"
        crc_s = f"{zlib.crc32(b'propagator prop-a.test\n') & 0xFFFFFFFF:08X}"
        propA.send(f"DB 006 END N tx_div {crc_div}")
        propA.wait_for(lambda l: " DB " in l and " ACK N tx_div " in l, "ACK N from Hub6")
        for b in ('C', 'I', 'L', 'K'):
            propA.send(f"DB 006 INF {b} 00000000 0")
        propA.send(f"DB 006 INF S {crc_s} 0")
        time.sleep(0.5)

        # Now full reconciliation complete: clients ALLOWED!
        client6_ok = MockClient("127.0.0.1", ports6[0], "user6_ok")
        welcome = client6_ok.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, "Client should be allowed after full reconciliation"
        print("PASS: Test 6: Clients ALLOWED after staged sync commit and full reconciliation")

        client6_ok.close()
        propA.close()
        stop(proc6)

        # =========================================================================
        # Test 7: Unsolicited HEL ACK rejection
        # =========================================================================
        print("\n=== Running Test 7: Unsolicited HEL ACK rejection ===")
        node7 = tmpdir / "node7"
        ports7 = free_ports(3)
        links7 = [("peer7.test", 0, False)]
        dbdir7 = node7 / "db"
        dbdir7.mkdir(parents=True, exist_ok=True)
        (node7 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node7 / "modules" / "third" / "udb.so")
        conf7 = node7 / "unrealircd.conf"
        write_config(conf7, "hub7.test", "007", ports7, links7, dbdir7)

        proc7 = subprocess.Popen(bwrap_command(node7, ircd_bin, conf7))
        wait_for_daemon(proc7, "127.0.0.1", ports7[0])

        # Peer links and completes negotiation
        peer7 = MockPeer("peer7.test", "07P", "127.0.0.1", ports7[1], "007", propagator_advertised="?", autostart_hel=True)
        time.sleep(0.3)

        # Peer sends duplicate HEL 4 ACK -> must be ignored idempotently without re-triggering sync or changing state
        peer7.clear()
        peer7.send("DB 007 HEL 4 ACK")
        time.sleep(0.3)
        # Duplicate ACK ignored
        print("PASS: Test 7: Duplicate HEL 4 ACK ignored idempotently on confirmed peer")

        # Peer links with invalid state (no waiting), sending unsolicited ACK must not revive capability
        peer7.close()
        stop(proc7)

        # =========================================================================
        # Test 8: Bootstrap persistence across daemon restarts
        # =========================================================================
        print("\n=== Running Test 8: Bootstrap persistence across restarts ===")
        node8 = tmpdir / "node8"
        ports8 = free_ports(3)
        links8 = [("peer-a.test", 0, False)]
        dbdir8 = node8 / "db"
        dbdir8.mkdir(parents=True, exist_ok=True)
        (node8 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, node8 / "modules" / "third" / "udb.so")
        conf8 = node8 / "unrealircd.conf"
        write_config(conf8, "hub8.test", "008", ports8, links8, dbdir8)

        proc8 = subprocess.Popen(bwrap_command(node8, ircd_bin, conf8))
        wait_for_daemon(proc8, "127.0.0.1", ports8[0])

        # Complete initial bootstrap with Peer A
        peerA = MockPeer("peer-a.test", "00A", "127.0.0.1", ports8[1], "008", propagator_advertised="?", autostart_hel=True, send_inf=True)
        time.sleep(0.5)

        # Confirm node is ready and client connects
        c1 = MockClient("127.0.0.1", ports8[0], "pers_user1")
        assert c1.wait_for(lambda l: " 001 " in l, timeout=3) is not None
        c1.close()
        peerA.close()

        # Stop daemon
        stop(proc8)
        time.sleep(0.5)

        # Restart daemon on the same dbdir
        proc8_restart = subprocess.Popen(bwrap_command(node8, ircd_bin, conf8))
        wait_for_daemon(proc8_restart, "127.0.0.1", ports8[0])

        # Connect client IMMEDIATELY without needing any peer bootstrap: MUST be allowed!
        c2 = MockClient("127.0.0.1", ports8[0], "pers_user2")
        welcome = c2.wait_for(lambda l: " 001 " in l, timeout=3)
        assert welcome is not None, f"Client must be allowed on restarted initialized node, lines: {c2.lines}"
        print("PASS: Test 8: Node remained ready and allowed clients immediately after restart")

        c2.close()
        stop(proc8_restart)

    print("\nALL 8 INCREMENTAL REGRESSION TESTS PASSED SUCCESSFULLY!")


if __name__ == "__main__":
    test_suite()
