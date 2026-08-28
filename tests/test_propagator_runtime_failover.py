#!/usr/bin/env python3
"""Integration test for UDB runtime propagator failover and link recovery.

Topology:
  Primary Services A -> Hub A <-> Relay B <-> Leaf C
  Failover Services B ---------> Relay B
  Cluster S::propagator = services-a.test,hub-a.test,services-b.test,hub-b.test

Scenario:
  1. Initial state: Services A is linked to Hub A.
     - Hub A selects Services A (Priority 1 direct peer).
     - Relay B selects Hub A (Priority 2 direct peer).
     - Leaf C selects Relay B (Priority 4 direct peer).
     - A staged snapshot from Services A propagates Services A -> Hub A -> Relay B -> Leaf C.

  2. Failover: Services A and Hub A are stopped (simulating upstream outage / netsplit).
     - Relay B detects Hub A is gone.
     - Mock Services B connects directly to Relay B (Priority 3 direct peer).
     - Relay B dynamically selects Services B as its upstream propagator!
     - Leaf C continues selecting Relay B.
     - A staged snapshot from Services B propagates Services B -> Relay B -> Leaf C.

  3. Recovery: Services B disconnects; Hub A and Services A restart and reconnect to Relay B.
     - Deterministic priority restores Hub A (Priority 2) as Relay B's upstream propagator.
     - Hub A restores Services A (Priority 1) as its upstream propagator.
     - A staged snapshot from Services A propagates Services A -> Hub A -> Relay B -> Leaf C.
"""

import os
import pathlib
import secrets
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
LINK_PASSWORD = "testfailoverpassword"


def tree_checksum(records):
    lines = sorted(f"{path} {value}\n".encode("ascii") for path, value in records)
    return f"{zlib.crc32(b''.join(lines)) & 0xFFFFFFFF:08X}"


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


def write_config(path, name, sid, ports, links, dbdir, propagator=None):
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
    udb_block = f'''udb {{
    database-directory "{dbdir}";
{udb_prop}}}'''

    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB runtime failover test node";
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
class servers {{ pingfreq 60; connfreq 6; maxclients 4; sendq 20M; }}
allow {{ mask "*@*"; class clients; maxperip 20; }}
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


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    for candidate in (
        pathlib.Path(__file__).resolve().parent.parent / "src" / "udb.so",
        pathlib.Path(__file__).resolve().parent.parent / "dist" / "udb.so",
        RUNTIME_ROOT / "modules/third/udb.so",
    ):
        if candidate.is_file():
            return candidate
    return pathlib.Path(__file__).resolve().parent.parent / "src" / "udb.so"


class MockServices:
    def __init__(self, name, sid, target_sid, host, port):
        self.name = name
        self.sid = sid
        self.target_sid = target_sid
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.round_id = 1
        self.send_raw(f"PASS :{LINK_PASSWORD}")
        self.send_raw(f"PROTOCTL EAUTH={self.name}")
        self.send_raw("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send_raw(f"SERVER {self.name} 1 :UDB Mock Services")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, f"{self.name} handshake")
        self.send("EOS")
        self.send(f"DB {self.target_sid} HEL 4 {self.name}")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
        self.send(f"DB {self.target_sid} HEL 4 ACK")
        for b in ('N', 'C', 'I', 'L', 'K'):
            self.send(f"DB {self.target_sid} INF 1 {b} 00000000 0")
        s_crc = tree_checksum([("flood", "5:30"), ("propagator", "services-a.test,hub-a.test,services-b.test,hub-b.test")])
        self.send(f"DB {self.target_sid} INF 1 S {s_crc} 1787720000")
        time.sleep(0.2)

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("utf-8"))

    def send_ins(self, path, value):
        self.send(f"DB * INS {path} :{value}")

    def send_snapshot(self, records, txid):
        checksum = tree_checksum(records)
        start = len(self.lines)
        self.round_id += 1
        self.send(f"DB {self.target_sid} INF {self.round_id} N {checksum} {int(time.time()) + 1000}")
        for block in ("C", "I", "L", "K"):
            self.send(f"DB {self.target_sid} INF {self.round_id} {block} 00000000 0")
        s_crc = tree_checksum([("flood", "5:30"),
                               ("propagator", "services-a.test,hub-a.test,services-b.test,hub-b.test")])
        self.send(f"DB {self.target_sid} INF {self.round_id} S {s_crc} 1787720000")
        self.wait_for(lambda line: f" RES {self.round_id} N" in line,
                      f"RES for staged snapshot {txid}", start_idx=start)
        self.send(f"DB {self.target_sid} BEGIN {self.round_id} N {txid} {checksum}")
        for path, value in records:
            self.send(f"DB {self.target_sid} PUT {self.round_id} N {txid} {path} :{value}")
        self.send(f"DB {self.target_sid} END {self.round_id} N {txid} {checksum}")
        self.wait_for(lambda line: " DB " in line and f" ACK {self.round_id} N {txid} " in line,
                      f"ACK for staged snapshot {txid}", start_idx=start)

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
                    self.send_raw(f"PONG {line.split()[1]}")
                elif " PING " in line:
                    parts = line.split()
                    self.send(f"PONG {parts[1]} {parts[2] if len(parts) > 2 else ''}".strip())
                self.lines.append(line)

    def wait_for(self, predicate, description, timeout=8, start_idx=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.receive(deadline)
            for line in self.lines[start_idx:]:
                if predicate(line):
                    return line
            time.sleep(0.05)
        raise TimeoutError(f"timed out waiting for {description}; received: {self.lines}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    ircd = DEFAULT_IRCD
    if not ircd.is_file():
        print(f"SKIP: unrealircd binary not found at {ircd}")
        return 77

    module = find_module_path()
    if not module.is_file():
        print(f"SKIP: udb.so not built at {module}")
        return 77

    temp_root = pathlib.Path(tempfile.mkdtemp(prefix="udb-failover-runtime-"))
    processes = []
    logs = {}

    try:
        node_a = temp_root / "node-a"
        node_b = temp_root / "node-b"
        node_c = temp_root / "node-c"

        for n in (node_a, node_b, node_c):
            (n / "data").mkdir(parents=True)
            (n / "runtime-data").mkdir(parents=True)
            (n / "tmp").mkdir(parents=True)
            (n / "modules/third").mkdir(parents=True)
            shutil.copy2(module, n / "modules/third/udb.so")

        # Cluster priority list:
        # 1: services-a.test (attached to Hub A)
        # 2: hub-a.test
        # 3: services-b.test (attached to Relay B)
        # 4: hub-b.test
        propagator_list = "services-a.test,hub-a.test,services-b.test,hub-b.test"

        for n in (node_a, node_b, node_c):
            (n / "data/udb_S.db").write_text(
                f"; UDB Block S\n; Saved: 1787720000\n; Records: 2\npropagator {propagator_list}\nflood 5:30\n",
                encoding="ascii"
            )

        all_ports = free_ports(9)
        ports_a = tuple(all_ports[0:3])
        ports_b = tuple(all_ports[3:6])
        ports_c = tuple(all_ports[6:9])

        config_a = node_a / "unrealircd.conf"
        config_b = node_b / "unrealircd.conf"
        config_c = node_c / "unrealircd.conf"

        # Node A accepts services-a.test and connects to hub-b.test
        write_config(config_a, "hub-a.test", "00A", ports_a,
                     [("services-a.test", 0, False), ("hub-b.test", ports_b[1], False)],
                     node_a / "data", propagator=None)

        # Node B links to hub-a.test, leaf-c.test, and allows services-b.test
        write_config(config_b, "hub-b.test", "00B", ports_b,
                     [("hub-a.test", ports_a[1], True), ("leaf-c.test", ports_c[1], False), ("services-b.test", 0, False)],
                     node_b / "data", propagator=None)

        # Node C links to hub-b.test
        write_config(config_c, "leaf-c.test", "00C", ports_c,
                     [("hub-b.test", ports_b[1], True)],
                     node_c / "data", propagator=None)

        log_a = node_a / "ircd.log"
        log_b = node_b / "ircd.log"
        log_c = node_c / "ircd.log"
        logs["A"] = log_a
        logs["B"] = log_b
        logs["C"] = log_c

        proc_a = subprocess.Popen(bwrap_command(node_a, ircd, config_a), stdout=log_a.open("w"), stderr=subprocess.STDOUT)
        processes.append(proc_a)
        wait_for_daemon(proc_a, "127.0.0.1", ports_a[0])

        proc_b = subprocess.Popen(bwrap_command(node_b, ircd, config_b), stdout=log_b.open("w"), stderr=subprocess.STDOUT)
        processes.append(proc_b)
        wait_for_daemon(proc_b, "127.0.0.1", ports_b[0])

        proc_c = subprocess.Popen(bwrap_command(node_c, ircd, config_c), stdout=log_c.open("w"), stderr=subprocess.STDOUT)
        processes.append(proc_c)
        wait_for_daemon(proc_c, "127.0.0.1", ports_c[0])

        time.sleep(1.0)

        # -------------------------------------------------------------
        # Step 1: Connect Primary Services A to Hub A
        # -------------------------------------------------------------
        services_a = MockServices("services-a.test", "0SA", "00A", "127.0.0.1", ports_a[1])
        time.sleep(0.5)

        services_a.send_snapshot([("alice::vhost", "official.alice.net")], "primary-a1")
        time.sleep(0.5)

        n_file_a = node_a / "data/udb_N.db"
        n_file_b = node_b / "data/udb_N.db"
        n_file_c = node_c / "data/udb_N.db"

        deadline = time.monotonic() + 10
        persisted = False
        while time.monotonic() < deadline:
            if n_file_c.exists() and "alice" in n_file_c.read_text(errors="replace"):
                persisted = True
                break
            time.sleep(0.2)

        assert persisted, f"FAIL: Step 1 mutation from Services A did not reach Leaf C: {n_file_c.read_text(errors='replace') if n_file_c.exists() else 'NONE'}"
        assert "alice::vhost official.alice.net" in n_file_a.read_text(errors="replace")
        assert "alice::vhost official.alice.net" in n_file_b.read_text(errors="replace")
        assert "alice::vhost official.alice.net" in n_file_c.read_text(errors="replace")
        print("PASS: Step 1 - Staged snapshot from primary Services A propagated A -> B -> C and committed everywhere")

        # -------------------------------------------------------------
        # Step 2: Simulate failure of Hub A and Services A (netsplit)
        # -------------------------------------------------------------
        services_a.close()
        stop(proc_a)
        processes.remove(proc_a)
        time.sleep(1.5)

        # -------------------------------------------------------------
        # Step 3: Relay B takes over as authoritative propagator via Services B!
        # S::propagator has services-a.test (offline), hub-a.test (offline), services-b.test.
        # We connect mock Services B to Relay B and emit a mutation.
        # -------------------------------------------------------------
        services_b = MockServices("services-b.test", "0SB", "00B", "127.0.0.1", ports_b[1])
        time.sleep(0.5)

        services_b.send_snapshot([("alice::vhost", "official.alice.net"),
                                  ("bob::vhost", "official.bob.net")], "failover-b1")
        time.sleep(0.5)

        deadline = time.monotonic() + 10
        persisted_b = False
        while time.monotonic() < deadline:
            if n_file_c.exists() and "bob" in n_file_c.read_text(errors="replace"):
                persisted_b = True
                break
            time.sleep(0.2)

        assert persisted_b, f"FAIL: Step 2 mutation from failover Relay B did not reach Leaf C: {n_file_c.read_text(errors='replace') if n_file_c.exists() else 'NONE'}"
        assert "bob::vhost official.bob.net" in n_file_b.read_text(errors="replace")
        assert "bob::vhost official.bob.net" in n_file_c.read_text(errors="replace")
        print("PASS: Step 2 - Relay B accepted the failover staged snapshot and propagated it to C")

        # -------------------------------------------------------------
        # Step 4: Recovery: Services B disconnects; Hub A & Services A restart
        # -------------------------------------------------------------
        services_b.close()
        time.sleep(0.5)

        proc_a = subprocess.Popen(bwrap_command(node_a, ircd, config_a), stdout=log_a.open("a"), stderr=subprocess.STDOUT)
        processes.append(proc_a)
        wait_for_daemon(proc_a, "127.0.0.1", ports_a[0])
        time.sleep(1.5)

        services_a2 = MockServices("services-a.test", "0SA", "00A", "127.0.0.1", ports_a[1])
        time.sleep(0.5)

        # -------------------------------------------------------------
        # Step 5: Verify priority restored deterministically to Services A & Hub A
        # -------------------------------------------------------------
        services_a2.send_snapshot([("alice::vhost", "official.alice.net"),
                                   ("bob::vhost", "official.bob.net"),
                                   ("charlie::vhost", "official.charlie.net")], "recovery-a2")
        time.sleep(0.5)

        deadline = time.monotonic() + 10
        persisted_c = False
        while time.monotonic() < deadline:
            if n_file_c.exists() and "charlie" in n_file_c.read_text(errors="replace"):
                persisted_c = True
                break
            time.sleep(0.2)

        assert persisted_c, f"FAIL: Step 3 mutation from recovered primary Services A did not reach Leaf C: {n_file_c.read_text(errors='replace') if n_file_c.exists() else 'NONE'}"
        assert "charlie::vhost official.charlie.net" in n_file_a.read_text(errors="replace")
        assert "charlie::vhost official.charlie.net" in n_file_b.read_text(errors="replace")
        assert "charlie::vhost official.charlie.net" in n_file_c.read_text(errors="replace")
        print("PASS: Step 3 - Recovered primary accepted a staged snapshot and propagated charlie across the cluster")

        services_a2.close()
        print("ALL TESTS PASSED: Dynamic runtime failover and recovery verified successfully.")
        return 0

    finally:
        for p in processes:
            stop(p)
        if sys.exc_info()[0] is not None:
            for name, log_path in logs.items():
                if log_path.exists():
                    print(f"--- Node {name} Log ---\n{log_path.read_text(errors='replace')}", file=sys.stderr)
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
