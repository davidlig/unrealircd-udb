#!/usr/bin/env python3
"""Targeted test suite for UDB Phase 4: Periodic anti-entropy verification.

Validates:
  Test 0: The default MANIFEST REQ interval remains 1800 seconds.
  Test 1: Convergent anti-entropy cycle (follower sends MANIFEST REQ, authority replies
          with matching MANIFEST ACK, verified, zero data transfer, status remains OK).
  Test 2: Silent mutation drop / divergence recovery (authority reports divergent manifest,
          follower latches DEGRADED, issues RES for divergent block, applies snapshot,
          and returns to OK).
  Test 3: Authority responds to inbound MANIFEST REQ from authorized downstream peer
          with 6 valid MANIFEST ACK lines.
  Test 4: Unauthorized peer sending MANIFEST REQ receives ERR 6 (FORBIDDEN).
  Test 5: Authority returning ERR cancels pending check and backs off gracefully.
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

from udb_state_seed import seed_ready_state

ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
LINK_PASSWORD = "testlinkpassword"
EMPTY_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


def test_default_interval_contract():
    print("--- Running Test 0: Default anti-entropy interval contract ---")
    header = (pathlib.Path(__file__).resolve().parent.parent / "src" / "udb_internal.h").read_text(
        encoding="utf-8")
    assert "#define UDB_DEFAULT_ANTI_ENTROPY_INTERVAL 1800" in header, \
        "default MANIFEST REQ interval must remain 1800 seconds"
    print("PASS: Test 0: Default MANIFEST REQ interval is 1800 seconds")


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


def write_config(path, name, sid, ports, links, dbdir, propagator=None, anti_entropy_interval=None):
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
    udb_block = f'''udb {{
{udb_prop}{udb_ae}}}'''

    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
include "{RUNTIME_ROOT}/conf/operclass.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB anti-entropy test node";
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
    def __init__(self, name, sid, host, port, target_sid, propagator_advertised=None, autostart_hel=True, send_inf=True, authorizes_us=True):
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
            prop = propagator_advertised if propagator_advertised is not None else (self.name if authorizes_us else "?")
            self.send(f"DB {self.target_sid} HEL 4 {prop} 0000000000000001 OCL")
            self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
            self.send(f"DB {self.target_sid} HEL 4 ACK {prop} 0000000000000001 OCL")
            if send_inf:
                for b in ('N', 'C', 'I', 'S', 'L', 'K'):
                    self.send(f"DB {self.target_sid} INF 1 {b} {EMPTY_SHA256} 0 0")

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


def compute_sha256(entries):
    canonical = "".join(f"{path} {val}\n" for path, val in sorted(entries))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


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


def test_convergent_cycle():
    print("--- Running Test 1: Convergent anti-entropy cycle (match) ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_ae_test1_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            (data_dir / f"udb_{b}.db").write_text("", encoding="ascii")

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

        client = connect_oper("127.0.0.1", ports[0], "oper1")
        status = client.udb_status()
        assert "UDB synchronization: OK" in status, f"Expected UDB synchronization: OK, got: {status}"

        # Clear peer buffer and wait for MANIFEST REQ from follower hub
        peer.clear()
        req_line = peer.wait_for(lambda l: " DB " in l and " MANIFEST REQ " in l,
                                 "MANIFEST REQ from follower", timeout=6)
        print(f"Received from follower: {req_line}")
        parts = req_line.split()
        # :001 DB 002 MANIFEST REQ <round>
        round_id = parts[5]

        # Reply with 6 matching MANIFEST ACK lines (EMPTY_SHA256, 0 records)
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 MANIFEST ACK {round_id} {b} 0 {EMPTY_SHA256} 0")

        # Give it a moment to process ACK
        time.sleep(0.5)

        # Check client UDB STATUS: must remain UDB synchronization: OK
        status = client.udb_status()
        assert "UDB synchronization: OK" in status, f"Expected UDB synchronization: OK after matching manifests, got: {status}"

        # Verify no RES lines were sent by hub
        res_lines = [l for l in peer.lines if " RES " in l]
        assert len(res_lines) == 0, f"Expected 0 RES lines, got: {res_lines}"
        print("PASS: Test 1: Convergent anti-entropy cycle verified (0 transfers, status OK)")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


def test_divergence_recovery():
    print("--- Running Test 2: Divergence detection and reconciliation recovery ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_ae_test2_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            (data_dir / f"udb_{b}.db").write_text("", encoding="ascii")

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

        client = connect_oper("127.0.0.1", ports[0], "oper2")
        status = client.udb_status()
        assert "UDB synchronization: OK" in status

        peer.clear()
        req_line = peer.wait_for(lambda l: " DB " in l and " MANIFEST REQ " in l,
                                 "MANIFEST REQ from follower", timeout=6)
        parts = req_line.split()
        round_id = parts[5]

        # Prepare a remote block N with 1 record
        n_records = [("alice", "user@host.invalid:1700000000")]
        remote_n_sha = compute_sha256(n_records)

        # Reply with MANIFEST ACKs where block N is divergent
        peer.send(f"DB 001 MANIFEST ACK {round_id} N 1 {remote_n_sha} 10")
        for b in ('C', 'I', 'S', 'L', 'K'):
            peer.send(f"DB 001 MANIFEST ACK {round_id} {b} 0 {EMPTY_SHA256} 10")

        # Follower must detect divergence, latch DEGRADED and send RES for N
        res_line = peer.wait_for(lambda l: " DB " in l and " RES " in l and f" {round_id} N" in l,
                                 "RES for divergent block N", timeout=3)
        print(f"Follower requested recovery: {res_line}")

        # Client status should report DEGRADED while reconciling
        status = client.udb_status()
        assert "UDB synchronization: DEGRADED" in status, f"Expected UDB synchronization: DEGRADED during recovery, got: {status}"

        # Authority sends snapshot for block N
        peer.send(f"DB 001 BEGIN {round_id} N stg1 {remote_n_sha} 10")
        peer.send(f"DB 001 PUT {round_id} N stg1 alice user@host.invalid:1700000000")
        peer.send(f"DB 001 END {round_id} N stg1 {remote_n_sha} 10")

        # Follower should ACK the block commit
        peer.wait_for(lambda l: " DB " in l and " ACK " in l and f" {round_id} N" in l,
                      "ACK from follower after staged commit", timeout=3)

        time.sleep(0.5)

        # Health must return to OK
        status = client.udb_status()
        assert "UDB synchronization: OK" in status, f"Expected UDB synchronization: OK after recovery commit, got: {status}"
        print("PASS: Test 2: Divergence detected, recovery triggered via RES, committed, returned to OK")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


def test_inbound_manifest_req():
    print("--- Running Test 3: Authority responds to inbound MANIFEST REQ ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_ae_test3_"))
    proc = None
    peer = None
    try:
        data_dir = tmp / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            (data_dir / f"udb_{b}.db").write_text("", encoding="ascii")

        ports = free_ports(3)
        links = [("leaf.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        # Authority node: no remote propagator configured (local standalone authority)
        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator=None)

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        # Peer is downstream leaf; leaf selects hub.test as propagator (authorizes_us=True)
        peer = MockPeer("leaf.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="hub.test", authorizes_us=True, send_inf=False)

        peer.clear()
        # Leaf sends MANIFEST REQ 99
        peer.send("DB 001 MANIFEST REQ 99")

        # Authority hub should reply with 6 MANIFEST ACK lines
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and len([l for l in peer.lines if " MANIFEST ACK 99 " in l]) < 6:
            peer.receive(deadline)

        ack_lines = [l for l in peer.lines if " MANIFEST ACK 99 " in l]
        assert len(ack_lines) == 6, f"Expected 6 MANIFEST ACK lines, got: {ack_lines}"
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            matching = [l for l in ack_lines if f" MANIFEST ACK 99 {b} 0 {EMPTY_SHA256} " in l]
            assert len(matching) == 1, f"Missing or invalid MANIFEST ACK for block {b}: {ack_lines}"

        print("PASS: Test 3: Inbound MANIFEST REQ correctly answered with 6 MANIFEST ACK lines")
    finally:
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


def test_unauthorized_manifest_req():
    print("--- Running Test 4: Unauthorized peer MANIFEST REQ is rejected ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_ae_test4_"))
    proc = None
    peer = None
    try:
        data_dir = tmp / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            (data_dir / f"udb_{b}.db").write_text("", encoding="ascii")

        ports = free_ports(3)
        links = [("rogue.test", ports[1], False)]
        mod_dir = tmp / "modules" / "third"
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(RUNTIME_ROOT / "modules/third/udb.so", mod_dir / "udb.so")

        conf = tmp / "unrealircd.conf"
        write_config(conf, "hub.test", "001", ports, links, data_dir, propagator=None)

        proc = subprocess.Popen(bwrap_command(tmp, DEFAULT_IRCD, conf))
        wait_for_daemon(proc, "127.0.0.1", ports[1])

        # Rogue peer does NOT authorize hub.test (authorizes_us=False)
        peer = MockPeer("rogue.test", "002", "127.0.0.1", ports[1], "001",
                        propagator_advertised="other.test", authorizes_us=False, send_inf=False)

        peer.clear()
        peer.send("DB 001 MANIFEST REQ 123")

        # Node must reject with ERR MANIFEST 6 123 0 (FORBIDDEN)
        err_line = peer.wait_for(lambda l: " DB " in l and " ERR MANIFEST 6 123 0" in l,
                                 "ERR MANIFEST 6 for unauthorized MANIFEST REQ", timeout=3)
        print(f"Node correctly rejected with: {err_line}")
        print("PASS: Test 4: Unauthorized MANIFEST REQ rejected with ERR 6 FORBIDDEN")
    finally:
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


def test_authority_err_handling():
    print("--- Running Test 5: Authority ERR response backoff handling ---")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="udb_ae_test5_"))
    proc = None
    peer = None
    client = None
    try:
        data_dir = tmp / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        seed_ready_state(data_dir, 1, int(time.time()))
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            (data_dir / f"udb_{b}.db").write_text("", encoding="ascii")

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

        client = connect_oper("127.0.0.1", ports[0], "oper5")
        status = client.udb_status()
        assert "UDB synchronization: OK" in status

        peer.clear()
        req_line = peer.wait_for(lambda l: " DB " in l and " MANIFEST REQ " in l,
                                 "MANIFEST REQ from follower", timeout=6)
        parts = req_line.split()
        round_id = parts[5]

        # Authority sends ERR for MANIFEST
        peer.send(f"DB 001 ERR MANIFEST 2 {round_id} 0")

        time.sleep(0.5)

        # Node should still be OK (transient check failed and backed off, not degraded)
        status = client.udb_status()
        assert "UDB synchronization: OK" in status, f"Expected UDB synchronization: OK, got: {status}"
        print("PASS: Test 5: Authority ERR handled gracefully with backoff; node remains stable")
    finally:
        if client:
            client.close()
        if peer:
            peer.close()
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    test_default_interval_contract()
    test_convergent_cycle()
    test_divergence_recovery()
    test_inbound_manifest_req()
    test_unauthorized_manifest_req()
    test_authority_err_handling()
    print("\nALL ANTI-ENTROPY TESTS PASSED SUCCESSFULLY!")
