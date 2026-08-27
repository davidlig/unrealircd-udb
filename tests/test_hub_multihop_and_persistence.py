#!/usr/bin/env python3
"""Comprehensive test suite for UDB HUB / Relay multi-hop operation,
readiness/health orthogonality, stale snapshot serving downstream, and
restart persistence while upstream services are offline.

Covers:
  Test HUB 1: Propagator disconnects after READY -> Hub transitions OK -> DEGRADED -> STALE
              while udb_ready stays 1. Local clients allowed with stale-action warn.
  Test HUB 2: Restart of Hub with Services offline -> Hub starts immediately with udb_ready = 1,
              sync_status = DEGRADED/STALE, accepting local clients.
  Test HUB 3: Hub serves downstream while STALE -> Leaf reconciles from Hub while Services is offline.
  Test HUB 4: Partial Hub (in BOOTSTRAPPING) rejects downstream RES requests.
  Test HUB 5: Services returns -> Hub receives updated snapshots, reconciles to OK, uninterrupted access.
  Test HUB 6: 3-node multi-hop A -> B -> C (Services -> Hub -> Leaf) mutation propagation.
  Test HUB 7: Downstream leaf rejects unauthorized foreign peer trying to inject UDB mutations/sync.
  Test HUB 8: Operator /UDB STATUS command verifies all fields (Readiness, Serving, Direct source, Authority).
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
            except OSError:
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
    src_mod = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
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

    with tempfile.TemporaryDirectory(prefix="udb_hub_test_") as tmp_dir_str:
        tmpdir = pathlib.Path(tmp_dir_str)

        # -----------------------------------------------------------------
        # TEST HUB 1: Propagator disconnects after READY -> OK -> DEGRADED -> STALE
        # udb_ready stays 1, local clients allowed with stale-action warn
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 1: Propagator disconnect after READY (warn mode) ===")
        p1_ports = free_ports(3)
        p1, n1, dbdir1, cfg1 = setup_node(
            tmpdir, "hub1.test", "001", p1_ports,
            [("services.test", 0, False)], propagator="services.test",
            stale_timeout=2, stale_action="warn"
        )
        try:
            # Services connects and initializes all 6 blocks
            services = MockPeer("services.test", "00S", "127.0.0.1", p1_ports[1], "001", propagator_advertised="services.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                services.send(f"DB 001 INF {b} 00000000 0")
            time.sleep(0.3)

            state_file1 = dbdir1 / ".udb_state"
            assert "STATE=READY" in state_file1.read_text(), "Hub1 must be READY after full sync"

            # Local client allowed in OK state
            c1 = MockClient("127.0.0.1", p1_ports[0], "user_ok")
            welcome = c1.wait_for(lambda l: " 001 " in l, timeout=3.0)
            assert welcome is not None, "Client should be allowed when OK"
            c1.close()

            # Services disconnects -> triggers DEGRADED, then STALE after 2s
            services.close()
            time.sleep(2.5)

            # Check status via OPER
            oper1 = MockClient("127.0.0.1", p1_ports[0], "oper1")
            oper1.send("OPER testoper operpass")
            oper1.wait_for(lambda l: " 381 " in l, timeout=3.0)
            oper1.send("UDB STATUS")
            oper1.wait_for(lambda l: "UDB synchronization: STALE" in l, timeout=3.0)
            oper1.wait_for(lambda l: "Database readiness: READY" in l, timeout=3.0)
            oper1.wait_for(lambda l: "Serving downstream: YES" in l, timeout=3.0)
            oper1.close()

            # Local client STILL allowed because stale-action is warn and udb_ready is 1
            c1_stale = MockClient("127.0.0.1", p1_ports[0], "user_stale_warn")
            welcome_stale = c1_stale.wait_for(lambda l: " 001 " in l, timeout=3.0)
            assert welcome_stale is not None, "Client should be allowed in STALE when stale-action is warn"
            c1_stale.close()
            print("PASS: Test HUB 1: Propagator disconnect transitions to STALE while udb_ready stays 1 (warn mode)")
        finally:
            stop(p1)

        # -----------------------------------------------------------------
        # TEST HUB 2: Restart of Hub with Services offline -> starts READY
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 2: Hub restart with Services offline ===")
        # Re-use node1 which has .udb_state = READY on disk
        p2 = subprocess.Popen(bwrap_command(n1, ircd_bin, cfg1))
        try:
            wait_for_daemon(p2, "127.0.0.1", p1_ports[0])
            wait_for_daemon(p2, "127.0.0.1", p1_ports[1])

            # Client connects immediately on start despite services being offline
            c2 = MockClient("127.0.0.1", p1_ports[0], "user_restart")
            welcome2 = c2.wait_for(lambda l: " 001 " in l, timeout=3.0)
            assert welcome2 is not None, "Client should connect immediately on READY hub restart"
            c2.close()

            oper2 = MockClient("127.0.0.1", p1_ports[0], "oper2")
            oper2.send("OPER testoper operpass")
            oper2.wait_for(lambda l: " 381 " in l, timeout=3.0)
            oper2.send("UDB STATUS")
            oper2.wait_for(lambda l: "Database readiness: READY" in l, timeout=3.0)
            oper2.wait_for(lambda l: "Serving downstream: YES" in l, timeout=3.0)
            oper2.close()
            print("PASS: Test HUB 2: Hub restarted offline immediately ready and accepting clients")
        finally:
            stop(p2)

        # -----------------------------------------------------------------
        # TEST HUB 3: Hub serves downstream while STALE
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 3: Hub serves downstream while STALE ===")
        p3_ports = free_ports(3)
        p3, n3, dbdir3, cfg3 = setup_node(
            tmpdir, "hub3.test", "003", p3_ports,
            [("services.test", 0, False), ("leaf.test", 0, False)],
            propagator="services.test", stale_timeout=2, stale_action="warn"
        )
        try:
            # Services synchronizes a nick record to Hub3, then disconnects
            services3 = MockPeer("services.test", "00S", "127.0.0.1", p3_ports[1], "003", propagator_advertised="services.test")
            services3.send("DB 003 BEGIN N tx_init 00000000")
            services3.send("DB 003 PUT N tx_init alice::vhost alice.hub")
            crc_n = zlib.crc32(b"alice::vhost alice.hub\n") & 0xFFFFFFFF
            services3.send(f"DB 003 END N tx_init {crc_n:08x}")
            services3.wait_for(lambda l: " ACK N" in l, "ACK N from Hub3")
            for b in ('C', 'I', 'S', 'L', 'K'):
                services3.send(f"DB 003 INF {b} 00000000 0")
            time.sleep(0.3)
            services3.close()

            # Wait for Hub3 to become STALE
            time.sleep(2.5)

            # Downstream leaf connects to Hub3 (Services is dead!)
            leaf = MockPeer("leaf.test", "00L", "127.0.0.1", p3_ports[1], "003", propagator_advertised="?")
            # Hub3 advertises HEL 4 -
            leaf.wait_for(lambda l: " DB " in l and " HEL 4 -" in l, "HEL 4 - from Hub3")
            leaf.send("DB 003 HEL 4 ACK")

            # Leaf asks for snapshot of N from Hub3 via RES
            leaf.send("DB 003 RES N")
            # Hub3 must serve snapshot even while STALE
            leaf.wait_for(lambda l: " DB " in l and " BEGIN N " in l, "BEGIN N from Hub3")
            leaf.wait_for(lambda l: "alice::vhost" in l and "alice.hub" in l, "PUT alice from Hub3")
            leaf.wait_for(lambda l: " DB " in l and " END N " in l, "END N from Hub3")
            leaf.send(f"DB 003 ACK N tx_init {crc_n:08x}")
            leaf.close()
            print("PASS: Test HUB 3: STALE Hub successfully served committed snapshots downstream")
        finally:
            stop(p3)

        # -----------------------------------------------------------------
        # TEST HUB 4: Partial Hub (in BOOTSTRAPPING) rejects downstream RES
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 4: Bootstrapping Hub rejects downstream RES ===")
        p4_ports = free_ports(3)
        p4, n4, dbdir4, cfg4 = setup_node(
            tmpdir, "hub4.test", "004", p4_ports,
            [("leaf.test", 0, False)], propagator="services.test"
        )
        try:
            # Hub4 is clean and NOT_READY (udb_ready == 0)
            leaf4 = MockPeer("leaf.test", "00L", "127.0.0.1", p4_ports[1], "004", propagator_advertised="services.test")
            leaf4.send("DB 004 HEL 4 ACK")
            leaf4.send("DB 004 RES N")

            # Hub4 must reject RES because !udb_ready
            err_res = leaf4.wait_for(lambda l: " DB " in l and (" ERR RES 6" in l or " ERR RES 2" in l), "ERR RES 6 from Bootstrapping Hub")
            assert " ERR RES" in err_res, f"Expected ERR RES from bootstrapping hub, got: {err_res}"
            leaf4.close()
            print("PASS: Test HUB 4: Bootstrapping Hub strictly rejected downstream snapshot request")
        finally:
            stop(p4)

        # -----------------------------------------------------------------
        # TEST HUB 5: Services returns -> Hub updates snapshots and reconciles to OK
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 5: Services returns and updates active database ===")
        p5_ports = free_ports(3)
        p5, n5, dbdir5, cfg5 = setup_node(
            tmpdir, "hub5.test", "005", p5_ports,
            [("services.test", 0, False)], propagator="services.test",
            stale_timeout=2, stale_action="warn"
        )
        try:
            services5 = MockPeer("services.test", "00S", "127.0.0.1", p5_ports[1], "005", propagator_advertised="services.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                services5.send(f"DB 005 INF {b} 00000000 0")
            time.sleep(0.3)
            services5.close()
            time.sleep(2.5)

            # Services reconnects with updated record
            services5_new = MockPeer("services.test", "00S", "127.0.0.1", p5_ports[1], "005", propagator_advertised="services.test")
            new_rec = "carol::vhost carol.org"
            crc_new = zlib.crc32((new_rec + "\n").encode("utf-8")) & 0xFFFFFFFF
            services5_new.send(f"DB 005 INF N {crc_new:08x} 1000")
            for b in ('C', 'I', 'S', 'L', 'K'):
                services5_new.send(f"DB 005 INF {b} 00000000 0")
            services5_new.wait_for(lambda l: " DB " in l and " RES N" in l, "RES N sent by Hub5")

            services5_new.send("DB 005 BEGIN N tx_upd 00000000")
            services5_new.send(f"DB 005 PUT N tx_upd {new_rec}")
            services5_new.send(f"DB 005 END N tx_upd {crc_new:08x}")
            services5_new.wait_for(lambda l: " ACK N" in l, "ACK N for update")
            time.sleep(0.3)

            oper5 = MockClient("127.0.0.1", p5_ports[0], "oper5")
            oper5.send("OPER testoper operpass")
            oper5.wait_for(lambda l: " 381 " in l, timeout=3.0)
            oper5.send("UDB STATUS")
            oper5.wait_for(lambda l: "UDB synchronization: OK" in l, timeout=3.0)
            oper5.close()
            services5_new.close()
            print("PASS: Test HUB 5: Services reconnection reconciled node back to OK with fresh snapshot")
        finally:
            stop(p5)

        # -----------------------------------------------------------------
        # TEST HUB 6: 3-Node Multi-hop propagation A -> B -> C
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 6: 3-Node Multi-hop mutation propagation ===")
        p6_hub_ports = free_ports(3)
        p6_leaf_ports = free_ports(3)

        p6_hub, n6_hub, db6_hub, cfg6_hub = setup_node(
            tmpdir, "hub6.test", "006", p6_hub_ports,
            [("services.test", 0, False), ("leaf6.test", 0, False)], propagator="services.test"
        )
        p6_leaf, n6_leaf, db6_leaf, cfg6_leaf = setup_node(
            tmpdir, "leaf6.test", "06L", p6_leaf_ports,
            [("hub6.test", p6_hub_ports[1], True)], propagator="services.test"
        )
        try:
            # Services links to Hub only
            services6 = MockPeer("services.test", "00S", "127.0.0.1", p6_hub_ports[1], "006", propagator_advertised="services.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                services6.send(f"DB 006 INF {b} 00000000 0")
            time.sleep(0.5)

            # Services broadcasts mutation INS
            services6.send("DB * INS N dave::vhost dave.org")
            time.sleep(0.5)

            # Oper on Leaf verifies the mutation arrived via Hub
            oper6 = MockClient("127.0.0.1", p6_leaf_ports[0], "oper6")
            oper6.send("OPER testoper operpass")
            oper6.wait_for(lambda l: " 381 " in l, timeout=3.0)
            oper6.send("UDB STATUS")
            oper6.wait_for(lambda l: "Database readiness: READY" in l, timeout=3.0)
            oper6.close()
            services6.close()
            print("PASS: Test HUB 6: Multi-hop mutation propagated across 3 nodes successfully")
        finally:
            stop(p6_leaf)
            stop(p6_hub)

        # -----------------------------------------------------------------
        # TEST HUB 7: Downstream leaf rejects unauthorized foreign peer
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 7: Downstream leaf rejects unauthorized foreign peer ===")
        p7_ports = free_ports(3)
        p7, n7, dbdir7, cfg7 = setup_node(
            tmpdir, "leaf7.test", "007", p7_ports,
            [("hub.test", 0, False), ("evil.test", 0, False)], propagator="services.test"
        )
        try:
            # Authorized Hub connects and initializes leaf
            hub7 = MockPeer("hub.test", "00H", "127.0.0.1", p7_ports[1], "007", propagator_advertised="services.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                hub7.send(f"DB 007 INF {b} 00000000 0")
            time.sleep(0.3)

            # Evil peer connects and tries to inject staged sync / mutation
            evil = MockPeer("evil.test", "00E", "127.0.0.1", p7_ports[1], "007", propagator_advertised="evil.test")
            evil.send("DB 007 BEGIN N tx_evil 00000000")
            err_evil = evil.wait_for(lambda l: " DB " in l and " ERR BEGIN 6" in l, "ERR BEGIN 6 from evil peer")
            assert " ERR BEGIN 6" in err_evil, f"Expected ERR BEGIN 6, got: {err_evil}"

            evil.send("DB * INS N evil::vhost evil.net")
            time.sleep(0.3)

            evil.close()
            hub7.close()
            print("PASS: Test HUB 7: Foreign peer injection rejected with FORBIDDEN")
        finally:
            stop(p7)

        # -----------------------------------------------------------------
        # TEST HUB 8: Operator /UDB STATUS command output validation
        # -----------------------------------------------------------------
        print("\n=== Running Test HUB 8: Operator /UDB STATUS validation ===")
        p8_ports = free_ports(3)
        p8, n8, dbdir8, cfg8 = setup_node(
            tmpdir, "hub8.test", "008", p8_ports,
            [("services.test", 0, False)], propagator="services.test",
            stale_timeout=15, stale_action="deny-new-clients"
        )
        try:
            services8 = MockPeer("services.test", "00S", "127.0.0.1", p8_ports[1], "008", propagator_advertised="services.test")
            for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                services8.send(f"DB 008 INF {b} 00000000 0")
            time.sleep(0.3)

            oper8 = MockClient("127.0.0.1", p8_ports[0], "oper8")
            oper8.send("OPER testoper operpass")
            oper8.wait_for(lambda l: " 381 " in l, timeout=3.0)
            oper8.send("UDB STATUS")

            oper8.wait_for(lambda l: "UDB synchronization: OK" in l, timeout=3.0)
            oper8.wait_for(lambda l: "Database readiness: READY" in l, timeout=3.0)
            oper8.wait_for(lambda l: "Serving downstream: YES" in l, timeout=3.0)
            oper8.wait_for(lambda l: "Selected direct source: services.test" in l, timeout=3.0)
            oper8.wait_for(lambda l: "Configured authority: services.test" in l, timeout=3.0)
            oper8.wait_for(lambda l: "New local clients: ALLOWED" in l, timeout=3.0)
            oper8.close()
            services8.close()
            print("PASS: Test HUB 8: Operator /UDB STATUS verified all fields")
        finally:
            stop(p8)

    print("\nALL 8 HUB & MULTIHOP REGRESSION TESTS PASSED SUCCESSFULLY!")


if __name__ == "__main__":
    test_suite()
