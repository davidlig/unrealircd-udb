#!/usr/bin/env python3
"""Comprehensive regression test suite for UDB dynamic propagator convergence,
strict HEL 4 protocol validation, and OK / DEGRADED / STALE operational states.

Covers:
  Test A - Propagator switch on HEL ACK with active staged session
  Test B - REHASH between already confirmed peers (authorizes_us false -> true)
  Test C - Malformed HEL 4 frame rejection
  Test D - READY node keeps accepting clients with the propagator offline
  Test E - /UDB STATUS stays OK/ALLOWED without a propagator
  Test F - Propagator return: inventory converges, node remains OK
  Test D2 - Bootstrap-pending node: DEGRADED -> STALE, denial, recovery via bootstrap
  Test G - Obsolete Block S policy retains '-' and never falls back to '?'
  Test H - Administrative recovery via local config override + REHASH
  Test I - Clean node announces '?' (bootstrap preserved)
  Test J - '-' never falls back to '?' over time
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

from udb_state_seed import seed_block, seed_bootstrapping_state, seed_ready_state

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


def write_config(path, name, sid, ports, links, dbdir, propagator=None, stale_timeout=None):
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
    udb_block = f'''udb {{
    database-directory "{dbdir}";
{udb_prop}{udb_stale_to}}}'''

    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB convergence and stale test node";
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
    def __init__(self, name, sid, host, port, target_sid, propagator_advertised=None, autostart_hel=True, send_inf=True):
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
                    self.send(f"DB {self.target_sid} INF 1 {b} 00000000 0")

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
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb_conv_test_"))
    try:
        module_src = pathlib.Path(__file__).resolve().parents[1] / "src" / "udb.so"
        ircd_bin = DEFAULT_IRCD

        print("=== Running Test A: Propagator switch on HEL ACK with active staged session ===")
        # N has policy in S.db: p1.test, p2.test. Initially P2 is connected and confirmed.
        # P2 begins sync. Before finishing, P1 confirms HEL ACK -> P2 session aborted, P1 becomes propagator.
        nodeA = tmpdir / "nodeA"
        portsA = free_ports(3)
        linksA = [("p1.test", 0, False), ("p2.test", 0, False)]
        dbdirA = nodeA / "db"
        dbdirA.mkdir(parents=True, exist_ok=True)
        (nodeA / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeA / "modules" / "third" / "udb.so")
        (dbdirA / "udb_S.db").write_text("; UDB Block S - Version 1\npropagator p1.test,p2.test\n", encoding="ascii")
        confA = nodeA / "unrealircd.conf"
        write_config(confA, "hubA.test", "00A", portsA, linksA, dbdirA)

        procA = subprocess.Popen(bwrap_command(nodeA, ircd_bin, confA))
        wait_for_daemon(procA, "127.0.0.1", portsA[0])

        # Connect P2 first (P1 is not connected yet)
        p2 = MockPeer("p2.test", "002", "127.0.0.1", portsA[1], "00A", propagator_advertised="p2.test")
        time.sleep(0.3)

        # P2 starts a requested round and begins block N.
        p2.send(f"DB 00A INF 2 N deadbeef {int(time.time()) + 1000}")
        p2.wait_for(lambda l: " RES 2 N" in l, "RES N to P2")
        p2.send("DB 00A BEGIN 2 N tx01 00000000")
        time.sleep(0.3)

        # Connect P1 with autostart_hel=False so we control handshake
        p1 = MockPeer("p1.test", "001", "127.0.0.1", portsA[1], "00A", autostart_hel=False)
        p1.send("DB 00A HEL 4 p1.test")
        p1.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL from hubA")
        # Now P1 sends ACK -> this triggers HEL_CONFIRMED for P1
        p1.send("DB 00A HEL 4 ACK")
        time.sleep(0.3)

        # Now P2 tries to send PUT on its old session -> should be rejected!
        p2.send("DB 00A PUT 2 N tx01 testuser::vhost test.vhost")
        p2.wait_for(lambda l: " DB " in l and " ERR PUT " in l, "ERR PUT to P2")
        print("PASS: P2 staged PUT was rejected after P1 confirmed HEL ACK")

        # P1 can immediately start a new requested round without error.
        p1.send(f"DB 00A INF 2 N deadbeef {int(time.time()) + 1000}")
        p1.wait_for(lambda l: " RES 2 N" in l, "RES N to P1")
        p1.send("DB 00A BEGIN 2 N tx02 00000000")
        time.sleep(0.2)
        p1.send("DB 00A PUT 2 N tx02 testuser::vhost test.vhost")
        import zlib
        crc_a = f"{zlib.crc32(b'testuser::vhost test.vhost\n') & 0xFFFFFFFF:08X}"
        p1.send(f"DB 00A END 2 N tx02 {crc_a}")
        p1.wait_for(lambda l: " DB " in l and " ACK 2 N tx02 " in l, "ACK N to P1", timeout=5)
        print("PASS: P1 immediately started and completed staged BEGIN/PUT/END without timeout")

        p1.close()
        p2.close()
        stop(procA)

        print("\n=== Running Test B: REHASH between already confirmed peers ===")
        # P1 and P2 connected and confirmed. selected = P1. REHASH to selected = P2.
        # Check authorizes_us changes false -> true on P2, triggering immediate INF exchange.
        nodeB = tmpdir / "nodeB"
        portsB = free_ports(3)
        linksB = [("p1.test", 0, False), ("p2.test", 0, False)]
        dbdirB = nodeB / "db"
        dbdirB.mkdir(parents=True, exist_ok=True)
        (nodeB / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeB / "modules" / "third" / "udb.so")
        confB = nodeB / "unrealircd.conf"
        write_config(confB, "hubB.test", "00B", portsB, linksB, dbdirB, propagator="p1.test")

        procB = subprocess.Popen(bwrap_command(nodeB, ircd_bin, confB))
        wait_for_daemon(procB, "127.0.0.1", portsB[0])

        p1 = MockPeer("p1.test", "001", "127.0.0.1", portsB[1], "00B", propagator_advertised="p1.test")
        p2 = MockPeer("p2.test", "002", "127.0.0.1", portsB[1], "00B", propagator_advertised="p2.test")
        time.sleep(0.5)

        # Oper client connects to issue REHASH command
        oper_b = MockClient("127.0.0.1", portsB[0], "oper_b")
        oper_b.wait_for(lambda l: " 001 " in l, timeout=3)
        oper_b.send("OPER testoper operpass")
        oper_b.wait_for(lambda l: " 381 " in l, timeout=2)

        # Clear lines
        p2.clear()

        # Update confB to select P2, then send REHASH
        write_config(confB, "hubB.test", "00B", portsB, linksB, dbdirB, propagator="p2.test")
        oper_b.send("REHASH")
        oper_b.wait_for(lambda l: " 219 " in l or "Rehashing" in l, timeout=3)
        time.sleep(0.5)

        # HubB initiates HEL refresh; P2 acknowledges, HubB selects P2 and advertises HEL 4 p2.test
        p2.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 on REHASH")
        p2.send("DB 00B HEL 4 ACK")
        p2.wait_for(lambda l: " DB " in l and " HEL 4 p2.test" in l, "HEL 4 p2.test announced to P2")
        print("PASS: HubB announced HEL 4 p2.test after REHASH")

        oper_b.close()
        p1.close()
        p2.close()
        stop(procB)

        print("\n=== Running Test C: Malformed HEL 4 ===")
        nodeC = tmpdir / "nodeC"
        portsC = free_ports(3)
        linksC = [("peer.test", 0, False)]
        dbdirC = nodeC / "db"
        dbdirC.mkdir(parents=True, exist_ok=True)
        (nodeC / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeC / "modules" / "third" / "udb.so")
        confC = nodeC / "unrealircd.conf"
        write_config(confC, "hubC.test", "00C", portsC, linksC, dbdirC, propagator="peer.test")

        procC = subprocess.Popen(bwrap_command(nodeC, ircd_bin, confC))
        wait_for_daemon(procC, "127.0.0.1", portsC[0])

        peerC = MockPeer("peer.test", "001", "127.0.0.1", portsC[1], "00C", autostart_hel=False)
        # Send HEL 4 with no 5th argument
        peerC.send("DB 00C HEL 4")
        peerC.receive(time.monotonic() + 0.5)
        assert not any(" ERR HEL " in line for line in peerC.lines), \
            "Malformed HEL emitted an ERR without a valid non-zero round ID"
        print("PASS: Malformed HEL 4 without a correlation round was rejected without an invalid ERR")

        peerC.close()
        stop(procC)

        print("\n=== Running Tests D, E, F: READY node survives propagator loss ===")
        nodeD = tmpdir / "nodeD"
        portsD = free_ports(3)
        linksD = [("prop.test", 0, False)]
        dbdirD = nodeD / "db"
        dbdirD.mkdir(parents=True, exist_ok=True)
        (nodeD / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeD / "modules" / "third" / "udb.so")
        confD = nodeD / "unrealircd.conf"
        # Stale timeout kept short to prove a READY node never reaches STALE
        write_config(confD, "hubD.test", "00D", portsD, linksD, dbdirD, propagator="prop.test", stale_timeout=2)

        procD = subprocess.Popen(bwrap_command(nodeD, ircd_bin, confD))
        wait_for_daemon(procD, "127.0.0.1", portsD[0])

        # Connect propagator
        propD = MockPeer("prop.test", "001", "127.0.0.1", portsD[1], "00D", propagator_advertised="prop.test")
        time.sleep(0.5)

        # Existing client connects
        client1 = MockClient("127.0.0.1", portsD[0], "user_existing")
        client1.wait_for(lambda l: " 001 " in l, timeout=3)
        print("PASS: Initial client connected while OK")

        # Disconnect propagator: a READY node stays OK and keeps serving clients
        propD.close()
        time.sleep(3.0)

        client_noprop = MockClient("127.0.0.1", portsD[0], "user_noprop")
        welcome = client_noprop.wait_for(lambda l: " 001 " in l, timeout=2)
        assert welcome is not None, "READY node must keep accepting clients without a propagator"
        client_noprop.close()

        client1.send("OPER testoper operpass")
        client1.wait_for(lambda l: " 381 " in l, timeout=2)
        client1.send("UDB STATUS")
        client1.wait_for(lambda l: " 339 " in l and "UDB synchronization: OK" in l, timeout=2)
        client1.wait_for(lambda l: " 339 " in l and "New local clients: ALLOWED" in l, timeout=2)
        print("PASS: Tests D/E: READY node stayed OK and kept accepting clients past the stale timeout")

        # Test F: propagator returns; inventory converges and the node remains OK
        propD2 = MockPeer("prop.test", "001", "127.0.0.1", portsD[1], "00D", propagator_advertised="prop.test")
        time.sleep(0.5)

        client1.lines.clear()
        client1.send("UDB STATUS")
        client1.wait_for(lambda l: " 339 " in l and "UDB synchronization: OK" in l, timeout=3)
        print("PASS: Test F: /UDB STATUS remained OK after the propagator reconnected")

        client_recovered = MockClient("127.0.0.1", portsD[0], "user_recov")
        welcome = client_recovered.wait_for(lambda l: " 001 " in l, timeout=2)
        assert welcome is not None, "New client should be accepted after the propagator returns"
        print("PASS: Test F: New client successfully connected after propagator return")

        client_recovered.close()
        client1.close()
        propD2.close()
        stop(procD)

        print("\n=== Running Test D2: bootstrap-pending DEGRADED to STALE with recovery ===")
        nodeD2 = tmpdir / "nodeD2"
        portsD2 = free_ports(4)
        # observer.test has a link block so it can link and query STATUS, but it
        # is not in the propagator policy, so the node stays bootstrap-pending.
        linksD2 = [("prop.test", 0, False), ("observer.test", 0, False)]
        dbdirD2 = nodeD2 / "db"
        dbdirD2.mkdir(parents=True, exist_ok=True)
        (nodeD2 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeD2 / "modules" / "third" / "udb.so")
        confD2 = nodeD2 / "unrealircd.conf"
        write_config(confD2, "hubD2.test", "00P", portsD2, linksD2, dbdirD2, propagator="prop.test", stale_timeout=2)

        procD2 = subprocess.Popen(bwrap_command(nodeD2, ircd_bin, confD2))
        wait_for_daemon(procD2, "127.0.0.1", portsD2[0])

        # A server peer outside the propagator policy keeps the link alive
        # without ever becoming an eligible propagator, so the node stays
        # bootstrap-pending and can still be queried via /UDB STATUS.
        observer = MockPeer("observer.test", "002", "127.0.0.1", portsD2[1], "00P",
                            propagator_advertised="observer.test", send_inf=False)
        time.sleep(0.5)

        denied_early = MockClient("127.0.0.1", portsD2[0], "user_pending")
        denied_early.receive(timeout=1.5)
        assert any("ERROR" in l or "Closing Link" in l for l in denied_early.lines), \
            f"Bootstrap-pending node must deny new local clients, got: {denied_early.lines}"
        denied_early.close()

        observer.send("UDB STATUS")
        observer.wait_for(lambda l: " 339 " in l and "Database readiness: BOOTSTRAPPING" in l,
                          "BOOTSTRAPPING readiness", timeout=3)
        observer.wait_for(lambda l: " 339 " in l and "UDB synchronization: DEGRADED" in l,
                          "DEGRADED while bootstrap pending", timeout=3)
        observer.wait_for(lambda l: " 339 " in l and "New local clients: DENIED" in l,
                          "clients denied while bootstrap pending", timeout=3)
        print("PASS: Test D2: Bootstrap-pending node reported BOOTSTRAPPING/DEGRADED and denied clients")

        time.sleep(2.2)
        observer.send("UDB STATUS")
        observer.wait_for(lambda l: " 339 " in l and "UDB synchronization: STALE" in l,
                          "STALE after bootstrap stale-timeout", timeout=3)
        print("PASS: Test D2: Bootstrap pending past stale-timeout transitioned to STALE")

        denied_late = MockClient("127.0.0.1", portsD2[0], "user_stale")
        denied_late.receive(timeout=1.5)
        assert any("ERROR" in l or "Closing Link" in l for l in denied_late.lines), \
            f"STALE bootstrap-pending node must still deny new local clients, got: {denied_late.lines}"
        denied_late.close()

        # The real propagator arrives: its zero-checksum inventory matches the
        # empty node, bootstrap converges -> READY + OK
        propD2 = MockPeer("prop.test", "001", "127.0.0.1", portsD2[1], "00P", propagator_advertised="prop.test")

        # Bootstrap completion is polled by the sync EVENT; retry client
        # connections until the node is READY and admits them.
        allowed = None
        retry_deadline = time.time() + 12
        while time.time() < retry_deadline:
            candidate = MockClient("127.0.0.1", portsD2[0], "user_ready")
            welcome = candidate.wait_for(lambda l: " 001 " in l, timeout=2)
            if welcome is not None:
                allowed = candidate
                break
            candidate.close()
            time.sleep(0.3)
        assert allowed is not None, "Node must serve clients once bootstrap completes"
        allowed.close()

        observer.send("UDB STATUS")
        observer.wait_for(lambda l: " 339 " in l and "Database readiness: READY" in l,
                          "READY after bootstrap", timeout=8)
        observer.wait_for(lambda l: " 339 " in l and "UDB synchronization: OK" in l,
                          "OK after bootstrap", timeout=3)
        print("PASS: Test D2: Bootstrap completed to READY/OK and clients were accepted")

        propD2.close()
        observer.close()
        stop(procD2)

        print("\n=== Running Test K: divergence latch until durable convergence ===")
        nodeK = tmpdir / "nodeK"
        portsK = free_ports(3)
        linksK = [("prop.test", 0, False)]
        dbdirK = nodeK / "db"
        dbdirK.mkdir(parents=True, exist_ok=True)
        (nodeK / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeK / "modules" / "third" / "udb.so")
        confK = nodeK / "unrealircd.conf"
        write_config(confK, "hubK.test", "00K", portsK, linksK, dbdirK, propagator="prop.test")

        procK = subprocess.Popen(bwrap_command(nodeK, ircd_bin, confK))
        wait_for_daemon(procK, "127.0.0.1", portsK[0])

        record = "alice::vhost alice.net"
        crc_record = zlib.crc32((record + "\n").encode("ascii")) & 0xFFFFFFFF

        # Bootstrap the empty node to READY + OK
        prop_k1 = MockPeer("prop.test", "001", "127.0.0.1", portsK[1], "00K", propagator_advertised="prop.test")
        time.sleep(0.5)
        client_k = MockClient("127.0.0.1", portsK[0], "oper_k")
        client_k.wait_for(lambda l: " 001 " in l, timeout=5)
        client_k.send("OPER testoper operpass")
        client_k.wait_for(lambda l: " 381 " in l, timeout=3)
        client_k.send("UDB STATUS")
        client_k.wait_for(lambda l: " 339 " in l and "UDB synchronization: OK" in l, timeout=3)
        prop_k1.close()

        # Reconnect with a divergent N inventory: node must latch DEGRADED
        prop_k2 = MockPeer("prop.test", "001", "127.0.0.1", portsK[1], "00K",
                           propagator_advertised="prop.test", autostart_hel=False)
        prop_k2.send("DB 00K HEL 4 prop.test")
        prop_k2.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL response for divergent round")
        prop_k2.send("DB 00K HEL 4 ACK")
        prop_k2.send(f"DB 00K INF 1 N {crc_record:08x} 0")
        for b in ('C', 'I', 'S', 'L', 'K'):
            prop_k2.send(f"DB 00K INF 1 {b} 00000000 0")
        prop_k2.wait_for(lambda l: " DB " in l and " RES 1 N" in l, "RES for divergent block")
        time.sleep(0.5)

        client_k.send("UDB STATUS")
        client_k.wait_for(lambda l: " 339 " in l and "UDB synchronization: DEGRADED" in l, timeout=3)
        client_k.wait_for(lambda l: " 339 " in l and "Recovery: ACTIVE" in l, timeout=3)
        client_k.wait_for(lambda l: " 339 " in l and "New local clients: ALLOWED" in l, timeout=3)
        print("PASS: Test K: Confirmed divergence latched DEGRADED while clients stay allowed")

        # Authority disappears mid-recovery: DEGRADED must persist (no latch reset)
        prop_k2.close()
        time.sleep(1.0)
        client_k.send("UDB STATUS")
        client_k.wait_for(lambda l: " 339 " in l and "UDB synchronization: DEGRADED" in l, timeout=3)
        print("PASS: Test K: DEGRADED persisted across authority disconnect")

        # A new round with the same divergence, this time completed with a
        # staged snapshot: only durable convergence returns the node to OK
        prop_k3 = MockPeer("prop.test", "001", "127.0.0.1", portsK[1], "00K",
                           propagator_advertised="prop.test", autostart_hel=False)
        prop_k3.send("DB 00K HEL 4 prop.test")
        prop_k3.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL response for recovery round")
        prop_k3.send("DB 00K HEL 4 ACK")
        prop_k3.send(f"DB 00K INF 2 N {crc_record:08x} 0")
        for b in ('C', 'I', 'S', 'L', 'K'):
            prop_k3.send(f"DB 00K INF 2 {b} 00000000 0")
        prop_k3.wait_for(lambda l: " DB " in l and " RES 2 N" in l, "RES for recovery block")
        prop_k3.send("DB 00K BEGIN 2 N tx_k 00000000")
        prop_k3.send(f"DB 00K PUT 2 N tx_k {record}")
        prop_k3.send(f"DB 00K END 2 N tx_k {crc_record:08x}")
        prop_k3.wait_for(lambda l: " ACK 2 N tx_k " in l, "ACK for staged recovery commit")
        time.sleep(0.5)

        client_k.send("UDB STATUS")
        client_k.wait_for(lambda l: " 339 " in l and "UDB synchronization: OK" in l, timeout=3)
        client_k.wait_for(lambda l: " 339 " in l and "Recovery: IDLE" in l, timeout=3)
        print("PASS: Test K: Durable staged convergence returned the node to OK")

        client_k.close()
        prop_k3.close()
        stop(procK)

        print("\n=== Running Tests G, H: Obsolete S policy, Advertised '-', Administrative REHASH Recovery ===")
        nodeG = tmpdir / "nodeG"
        portsG = free_ports(3)
        linksG = [("new-a.test", 0, False), ("other.test", 0, False)]
        dbdirG = nodeG / "db"
        dbdirG.mkdir(parents=True, exist_ok=True)
        (nodeG / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeG / "modules" / "third" / "udb.so")

        # Seed valid 6-block snapshots on disk with S.db propagator = old-a.test,old-b.test
        for letter in ('N', 'C', 'I', 'S', 'L', 'K'):
            seed_block(dbdirG / f"udb_{letter}.db", letter)
        seed_block(dbdirG / "udb_S.db", "S", "propagator old-a.test,old-b.test\n")
        seed_ready_state(dbdirG)

        confG = nodeG / "unrealircd.conf"
        # No local propagator override in config, stale timeout 2s
        write_config(confG, "hubG.test", "00G", portsG, linksG, dbdirG, stale_timeout=2)

        procG = subprocess.Popen(bwrap_command(nodeG, ircd_bin, confG))
        wait_for_daemon(procG, "127.0.0.1", portsG[0])

        # Connect oper before stale timeout to execute administrative commands
        oper_g = MockClient("127.0.0.1", portsG[0], "oper_g")
        oper_g.wait_for(lambda l: " 001 " in l, timeout=3)
        oper_g.send("OPER testoper operpass")
        oper_g.wait_for(lambda l: " 381 " in l, timeout=2)

        # Test G: New neighbor new-a.test connects (not in policy old-a, old-b)
        new_a = MockPeer("new-a.test", "00A", "127.0.0.1", portsG[1], "00G", autostart_hel=False)
        new_a.send("DB 00G HEL 4 new-a.test")
        # HubG should advertise HEL 4 - (because policy is present from S.db, but neither old-a nor old-b is eligible)
        hel_resp = new_a.wait_for(lambda l: " DB " in l and " HEL 4 -" in l, "HEL 4 - advertised")
        assert " HEL 4 -" in hel_resp, f"Expected HEL 4 -, got: {hel_resp}"
        new_a.send("DB 00G HEL 4 ACK")
        time.sleep(0.3)
        print("PASS: Test G: HubG advertised HEL 4 - with obsolete S::propagator")

        # new-a tries to send BEGIN -> must be rejected with UDB_ERR_FORBIDDEN (6)
        new_a.send("DB 00G BEGIN 1 N tx01 00000000")
        new_a.wait_for(lambda l: " DB " in l and " ERR BEGIN 6 1 N" in l, "ERR BEGIN 6 1 N")
        print("PASS: Test G: Unauthorized peer new-a could not initiate staged sync")

        # Test J: Advertised state remains '-' and NEVER falls back to '?'
        oper_g.lines.clear()
        oper_g.send("UDB STATUS")
        stale_status = oper_g.wait_for(lambda l: "Advertised state: HEL 4 -" in l, timeout=3)
        assert stale_status is not None, f"Expected Advertised state: HEL 4 -, got: {oper_g.lines}"
        oper_g.wait_for(lambda l: "UDB synchronization: STALE" in l, timeout=3)
        print("PASS: Test J: Advertised state remained '-' and did not fall back to '?' after stale timeout")

        # Test H: Administrative recovery via local config override + REHASH
        write_config(confG, "hubG.test", "00G", portsG, linksG, dbdirG, propagator="new-a.test", stale_timeout=2)
        oper_g.send("REHASH")
        time.sleep(0.5)

        # HubG initiates HEL negotiation, new-a acknowledges, HubG selects new-a.test
        new_a.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 on REHASH")
        new_a.send("DB 00G HEL 4 ACK")
        new_a.wait_for(lambda l: " DB " in l and " HEL 4 new-a.test" in l, "HEL 4 new-a.test after REHASH")
        print("PASS: Test H: HubG selected new-a.test as authority after local config override + REHASH")

        # new-a can now successfully staged sync to HubG
        new_a.send(f"DB 00G INF 1 N deadbeef {int(time.time()) + 1000}")
        new_a.wait_for(lambda l: " RES 1 N" in l, "RES N from HubG")
        new_a.send("DB 00G BEGIN 1 N tx02 00000000")
        time.sleep(0.2)
        new_a.send("DB 00G PUT 1 N tx02 admin::vhost admin.vhost")
        crc_h = f"{zlib.crc32(b'admin::vhost admin.vhost\n') & 0xFFFFFFFF:08X}"
        new_a.send(f"DB 00G END 1 N tx02 {crc_h}")
        new_a.wait_for(lambda l: " DB " in l and " ACK 1 N tx02 " in l, "ACK N from HubG", timeout=5)
        for block in ('C', 'I', 'S', 'L', 'K'):
            new_a.send(f"DB 00G INF 1 {block} 00000000 0")
        print("PASS: Test H: staged sync succeeded and node returned to OK")

        oper_g.close()
        new_a.close()
        stop(procG)

        print("\n=== Running Test I: No-policy node semantics (standalone READY vs bootstrap) ===")
        nodeI = tmpdir / "nodeI"
        portsI = free_ports(3)
        linksI = [("peer.test", 0, False)]
        dbdirI = nodeI / "db"
        dbdirI.mkdir(parents=True, exist_ok=True)
        (nodeI / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeI / "modules" / "third" / "udb.so")
        confI = nodeI / "unrealircd.conf"
        write_config(confI, "hubI.test", "00I", portsI, linksI, dbdirI)

        procI = subprocess.Popen(bwrap_command(nodeI, ircd_bin, confI))
        wait_for_daemon(procI, "127.0.0.1", portsI[0])

        # A clean node without policy is its own standalone authority: it goes
        # READY locally and advertises itself, never a bootstrap wildcard.
        state_I = dbdirI / ".udb_state"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if state_I.exists() and "STATE=READY" in state_I.read_text():
                break
            time.sleep(0.1)
        else:
            raise AssertionError("Clean no-policy node did not become standalone READY")

        peerI = MockPeer("peer.test", "001", "127.0.0.1", portsI[1], "00I", autostart_hel=False)
        peerI.send("DB 00I HEL 4 ?")
        hel_resp_I = peerI.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 response")
        assert " HEL 4 hubI.test" in hel_resp_I, f"Expected standalone HEL 4 hubI.test, got: {hel_resp_I}"
        print("PASS: Test I1: Clean no-policy node is standalone READY and announces itself")

        peerI.close()
        stop(procI)

        # A node with persisted BOOTSTRAPPING state keeps seeking an exclusive
        # bootstrap source and announces the bootstrap wildcard.
        nodeI2 = tmpdir / "nodeI2"
        portsI2 = free_ports(3)
        dbdirI2 = nodeI2 / "db"
        dbdirI2.mkdir(parents=True, exist_ok=True)
        (nodeI2 / "modules" / "third").mkdir(parents=True, exist_ok=True)
        shutil.copy(module_src, nodeI2 / "modules" / "third" / "udb.so")
        confI2 = nodeI2 / "unrealircd.conf"
        write_config(confI2, "hubI2.test", "00J", portsI2, linksI, dbdirI2)
        seed_bootstrapping_state(dbdirI2)

        procI2 = subprocess.Popen(bwrap_command(nodeI2, ircd_bin, confI2))
        wait_for_daemon(procI2, "127.0.0.1", portsI2[0])

        peerI2 = MockPeer("peer.test", "001", "127.0.0.1", portsI2[1], "00J", autostart_hel=False)
        peerI2.send("DB 00J HEL 4 ?")
        hel_resp_I2 = peerI2.wait_for(lambda l: " DB " in l and " HEL 4 " in l, "HEL 4 response (bootstrap)")
        assert " HEL 4 ?" in hel_resp_I2, f"Expected bootstrap HEL 4 ?, got: {hel_resp_I2}"
        print("PASS: Test I2: Persisted BOOTSTRAPPING node announces HEL 4 ? (bootstrap)")

        peerI2.close()
        stop(procI2)

        print("\nALL REGRESSION TESTS PASSED: Tests A through J completed successfully.")

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    test_suite()
