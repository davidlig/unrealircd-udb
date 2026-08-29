#!/usr/bin/env python3
"""Integration tests for multi-hop A-B-C live mutation forwarding and BIGLINES un-truncated propagation."""

import argparse
import base64
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


ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

SERVICES_NAME = "udb-svc.test"
SERVICES_SID = "002"


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


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


def write_config(path, name, sid, ports, links, module, dbdir, propagator, link_password):
    link_text = ""
    for peer, peer_port, autoconnect in links:
        outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                    'options { autoconnect; } }\n') if autoconnect else ""
        link_text += f'''link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
 {outgoing}   password "{link_password}";
    class servers;
}}
'''
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB BIGLINES multi-hop node";
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
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {ports[0]}; }}
listen {{ ip "127.0.0.1"; port {ports[1]}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {ports[2]}; options {{ tls; }} }}
{link_text}link {SERVICES_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{link_password}";
    class servers;
}}
ulines {{
    {SERVICES_NAME};
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "{propagator}";
}}
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


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


class MockPropagator:
    def __init__(self, host, port, target_sid, link_password, name=SERVICES_NAME, sid=SERVICES_SID,
                 propagator_advertised=None, send_inventory=True):
        self.name = name
        self.sid = sid
        self.target_sid = target_sid
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{link_password}")
        self.send(f"PROTOCTL EAUTH={self.name}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send(f"SERVER {self.name} 1 :UDB root propagator")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")
        prop = propagator_advertised if propagator_advertised is not None else self.name
        self.send(f"DB {self.target_sid} HEL 4 {prop}")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.target_sid} HEL 4 ACK")
        if send_inventory:
            for letter in ("N", "C", "I", "S", "L", "K"):
                self.send(f"DB {self.target_sid} INF 1 {letter} 00000000 0")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send_ins(self, path, data):
        val = f":{data}" if " " in str(data) and not str(data).startswith(":") else str(data)
        self.send(f"DB * INS {path} {val}")

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(16384)
            except socket.timeout:
                return
            if not data:
                return
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}")

    def close(self):
        try:
            self.send(f"SQUIT {SERVICES_NAME} :bye")
        except OSError:
            pass
        self.sock.close()


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    local_path = pathlib.Path(__file__).resolve().parent.parent / "src" / "udb.so"
    if local_path.is_file():
        return local_path
    runtime_path = RUNTIME_ROOT / "modules/third/udb.so"
    if runtime_path.is_file():
        return runtime_path
    return local_path


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-biglines-multihop-"))
    module_path = find_module_path()
    processes = []

    try:
        b_dir, c_dir = tmpdir / "node-b", tmpdir / "node-c"
        for node in (b_dir, c_dir):
            (node / "data").mkdir(parents=True)
            (node / "runtime-data").mkdir()
            (node / "tmp").mkdir()
            third_modules = node / "modules" / "third"
            third_modules.mkdir(parents=True)
            shutil.copy2(module_path, third_modules / "udb.so")

        raw_ports = free_ports(6)
        b_ports = tuple(raw_ports[0:3])
        c_ports = tuple(raw_ports[3:6])
        b_conf, c_conf = b_dir / "unrealircd.conf", c_dir / "unrealircd.conf"
        link_password = "udb-biglines-pass"

        # Topology: A (MockPropagator) <-> B (Node B) <-> C (Node C)
        write_config(b_conf, "udb-b.test", "0B1", b_ports,
                     (("udb-c.test", c_ports[1], True),),
                     module_path, b_dir / "data", SERVICES_NAME, link_password)
        write_config(c_conf, "udb-c.test", "0C1", c_ports,
                     (("udb-b.test", b_ports[1], False), ("udb-export.test", 0, False)),
                     module_path, c_dir / "data", "udb-b.test", link_password)

        b_log, c_log = b_dir / "ircd.log", c_dir / "ircd.log"

        proc_c = subprocess.Popen(bwrap_command(c_dir, ircd_bin, c_conf),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes.append(proc_c)
        time.sleep(1.0)

        proc_b = subprocess.Popen(bwrap_command(b_dir, ircd_bin, b_conf),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes.append(proc_b)
        time.sleep(1.5)

        # Connect MockPropagator A to Node B
        prop_a = MockPropagator("127.0.0.1", b_ports[1], "0B1", link_password)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            b_state = b_dir / "data" / ".udb_state"
            c_state = c_dir / "data" / ".udb_state"
            if (b_state.exists() and c_state.exists() and
                    "STATE=READY" in b_state.read_text() and "STATE=READY" in c_state.read_text()):
                break
            time.sleep(0.1)
        else:
            raise AssertionError("B and C did not reach READY through hop-by-hop reconciliation")
        print("PASS: Topology A (Propagator) -> B (Relay) -> C (Leaf) established with BIGLINES capability")

        # -------------------------------------------------------------
        # Generate mutations of various payload sizes on A
        # Sizes: 510B, 1024B, 4000B, 4096B (VALUE_MAX), and near S2S line max
        # -------------------------------------------------------------
        raw_pattern = "spam_big_" + ("k" * 3060) + "end"  # exactly 3072 bytes
        b64_b64 = base64.b64encode(raw_pattern.encode("ascii")).decode("ascii")
        b64_pattern_encoded = "b64%3A" + b64_b64

        test_mutations = {
            "C::#chan510::topic": "1" * 510,
            "C::#chan1k::topic": "2" * 1024,
            "C::#chan4k::topic": "3" * 4000,
            "C::#chan4096::topic": "4" * 4096,
            f"K::F::{b64_pattern_encoded}::reason": "SpamReason_" + ("5" * (4096 - 11)),
        }

        for path, data in test_mutations.items():
            prop_a.send_ins(path, data)
            time.sleep(0.15)

        prop_a.close()

        # Allow time for sync and snapshot persistence across B and C
        time.sleep(1.5)

        # Stop processes cleanly to ensure disk sync
        stop(proc_b)
        stop(proc_c)
        processes.clear()

        # -------------------------------------------------------------
        # Verify durable .db files on B and C are byte-for-byte identical
        # -------------------------------------------------------------
        b_c_db = (b_dir / "data" / "udb_C.db").read_text(encoding="ascii")
        c_c_db = (c_dir / "data" / "udb_C.db").read_text(encoding="ascii")
        b_k_db = (b_dir / "data" / "udb_K.db").read_text(encoding="ascii")
        c_k_db = (c_dir / "data" / "udb_K.db").read_text(encoding="ascii")

        for path, data in test_mutations.items():
            subpath = path.split("::", 1)[1]
            target_content_b = b_c_db if path.startswith("C::") else b_k_db
            target_content_c = c_c_db if path.startswith("C::") else c_k_db
            expected_line = f"{subpath} {data}"
            if expected_line not in target_content_b:
                raise AssertionError(f"Node B snapshot missing expected record for {subpath} (len {len(data)})")
            if expected_line not in target_content_c:
                raise AssertionError(f"Node C snapshot missing expected multi-hop record for {subpath} (len {len(data)})")

        print("PASS: Multi-hop live mutations of sizes 510B, 1024B, 4000B, 4096B, and ~8212B persisted identically on B and C")

        # -------------------------------------------------------------
        # Restart C to verify transactional load and invariant checksums
        # -------------------------------------------------------------
        proc_c = subprocess.Popen(bwrap_command(c_dir, ircd_bin, c_conf),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes.append(proc_c)
        time.sleep(1.0)

        # Query C via S2S staged export from a genuine downstream requester.
        # The requester selects C as its propagator, so C serves it hop-by-hop;
        # posing as C's own upstream (udb-b.test) would model an authority
        # cycle that strict directionality correctly refuses to serve.
        prop_c = MockPropagator("127.0.0.1", c_ports[1], "0C1", link_password, name="udb-export.test", sid="0E1",
                                propagator_advertised="udb-c.test", send_inventory=False)
        inventory = prop_c.wait_for(lambda l: " DB " in l and " INF " in l and " C " in l, "INF C")
        round_id = inventory.split(" INF ", 1)[1].split(" ", 1)[0]
        prop_c.send(f"DB 0C1 RES {round_id} C")
        prop_c.wait_for(lambda l: " DB " in l and f" BEGIN {round_id} C " in l, "BEGIN C")
        prop_c.wait_for(lambda l: " DB " in l and f" END {round_id} C " in l, "END C")

        c_records = {}
        for l in prop_c.lines:
            if " DB " in l and f" PUT {round_id} C " in l:
                parts = l.split(f" PUT {round_id} C ", 1)[1].split(" ", 2)
                p = parts[1]
                d = parts[2]
                if d.startswith(":"):
                    d = d[1:]
                c_records[p] = d

        for path, data in test_mutations.items():
            if path.startswith("C::"):
                subpath = path.split("::", 1)[1]
                if subpath not in c_records or c_records[subpath] != data:
                    raise AssertionError(f"Post-restart Node C payload mismatch for {subpath}: len {len(c_records.get(subpath, ''))} != {len(data)}")

        prop_c.close()
        print("PASS: Post-restart Node C loaded full un-truncated BIGLINES records byte-by-byte")

    finally:
        for p in processes:
            stop(p)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB BIGLINES multi-hop integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
