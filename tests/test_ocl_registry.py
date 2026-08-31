#!/usr/bin/env python3
"""Integration tests for the UDB operclass registry (OCL) and global view (OCLG).

Covers the HEL 4 OCL contract, local inventory replay, remote snapshot staging,
atomic commit, OCLG snapshots to an explicit subscriber, protocol violations,
and origin removal on disconnect.
"""

import argparse
import hashlib
import os
import pathlib
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

PEER_NAME = "udb-peer.test"
PEER_SID = "002"
IRCD_SID = "001"
LINK_PASSWORD = "udb-ocl-link-password"
HEX64 = re.compile(r"^[0-9a-f]{64}$")
EPOCH16 = re.compile(r"^[0-9a-f]{16}$")


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB operclass registry harness";
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
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
link {PEER_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}
operclass udbtest {{
	permissions {{
		channel {{
			oper {{ }}
		}}
	}}
}}
operclass udbtest-child {{
	parent udbtest;
	permissions {{
		channel {{
			oper {{ }}
		}}
	}}
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
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


def ocl_inventory_digest(entries):
    """Mirror of the C inventory digest: UDB-OCL-INVENTORY-v1 0x00 count entries."""
    h = hashlib.sha256()
    h.update(b"UDB-OCL-INVENTORY-v1")
    h.update(b"\x00")
    h.update(struct.pack(">I", len(entries)))
    for name, digest in entries:
        nb = name.encode("ascii")
        h.update(struct.pack(">I", len(nb)))
        h.update(nb)
        h.update(digest.encode("ascii"))
        h.update(b"\x00")
    return h.hexdigest()


class MockPeer:
    """A plain (non-ULine) server that participates in OCL and subscribes OCLG."""

    def __init__(self, host, port, subscribe_oclg=True):
        self.sid = PEER_SID
        self.ircd_sid = IRCD_SID
        self.oclg = subscribe_oclg
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.epoch = None
        self.generation = None
        self.send("PASS :" + LINK_PASSWORD)
        self.send(f"PROTOCTL EAUTH={PEER_NAME}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send(f"SERVER {PEER_NAME} 1 :UDB test peer")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def negotiate(self, ocl=True):
        """Send the HEL request, answer the IRCd HEL request, and return."""
        token = " OCL" if ocl else ""
        oclg = " OCLG" if (ocl and self.oclg) else ""
        self.send(f"DB {self.ircd_sid} HEL 4 ?{token}{oclg}")
        response = self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK{token}")
        return response

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
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, timeout=5):
        deadline = time.monotonic() + timeout
        start = len(self.lines)
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}")

    def drain(self, seconds=0.3):
        deadline = time.monotonic() + seconds
        self.receive(deadline)

    def collect_ocl_items(self):
        items = []
        for line in self.lines:
            m = re.search(r" DB \* OCL ITEM \S+ [0-9a-f]{16} \d+ (\S+) ([0-9a-f]{64})\r?$", line)
            if m:
                items.append((m.group(1), m.group(2)))
        return items

    def collect_oclg_snapshots(self):
        """Return [(generation, state, [(name, digest), ...]), ...] observed so far."""
        snapshots = []
        current = None
        for line in self.lines:
            m = re.search(r" DB \S+ OCLG BEGIN [0-9a-f]{16} (\d+) (READY|INCOMPLETE) (\d+) [0-9a-f]{64}", line)
            if m:
                current = (int(m.group(1)), m.group(2), [])
                continue
            if current is not None:
                m = re.search(r" DB \S+ OCLG ITEM [0-9a-f]{16} (\d+) (\S+) ([0-9a-f]{64})", line)
                if m:
                    current[2].append((m.group(2), m.group(3)))
                    continue
                if re.search(r" DB \S+ OCLG END [0-9a-f]{16} \d+", line):
                    snapshots.append(current)
                    current = None
        return snapshots

    def send_ocl_snapshot(self, epoch, generation, entries, count=None, aggregate=None):
        if count is None:
            count = len(entries)
        if aggregate is None:
            aggregate = ocl_inventory_digest(entries)
        self.send(f"DB * OCL BEGIN {self.sid} {epoch} {generation} {count} {aggregate}")
        for name, digest in entries:
            self.send(f"DB * OCL ITEM {self.sid} {epoch} {generation} {name} {digest}")
        self.send(f"DB * OCL END {self.sid} {epoch} {generation}")

    def close(self):
        try:
            self.send(f"SQUIT {PEER_NAME} :bye")
        except OSError:
            pass
        self.sock.close()


class DaemonLogReader:
    def __init__(self, proc):
        self.proc = proc
        self.output = ""
        self.lines = []
        self.buffer = ""

    def read_available(self):
        if not self.proc or not self.proc.stdout:
            return
        while True:
            readable, _, _ = select.select([self.proc.stdout], [], [], 0)
            if not readable:
                break
            try:
                chunk = os.read(self.proc.stdout.fileno(), 65536)
            except OSError:
                break
            if not chunk:
                break
            text = chunk.decode("utf-8", errors="replace")
            self.output += text
            self.buffer += text
            while "\n" in self.buffer:
                line, self.buffer = self.buffer.split("\n", 1)
                self.lines.append(line.rstrip("\r"))

    def wait_for(self, predicate, description, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read_available()
            for l in self.lines:
                if predicate(l):
                    return l
            if self.proc.poll() is not None:
                self.read_available()
                for l in self.lines:
                    if predicate(l):
                        return l
                raise AssertionError(
                    f"Daemon process exited (code {self.proc.returncode}) while waiting for log: {description}\n"
                    f"Captured logs:\n{self.output}"
                )
            time.sleep(0.02)
        raise AssertionError(f"Timeout waiting for daemon log: {description}\nCaptured logs:\n{self.output}")


def log_count_containing(logs, needle):
    return sum(1 for l in logs.lines if needle in l)


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
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-ocl-registry-"))
    module_path = find_module_path()
    proc = None

    try:
        node = tmpdir / "node"
        data_dir = node / "data"
        data_dir.mkdir(parents=True)
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(module_path, third_modules / "udb.so")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-ocl.test", IRCD_SID, client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        logs = DaemonLogReader(proc)
        time.sleep(1.0)
        if proc.poll() is not None:
            logs.read_available()
            raise RuntimeError(f"ircd failed to start:\n{logs.output}")

        # ---------------------------------------------------------------
        # Test 1: legacy HEL 4 without the mandatory OCL token is rejected
        # ---------------------------------------------------------------
        legacy = MockPeer("127.0.0.1", server_port, subscribe_oclg=False)
        legacy.oclg = False
        legacy.send(f"DB {IRCD_SID} HEL 4 ?")
        logs.wait_for(lambda l: "Link aborted: HEL 4 request without the mandatory OCL" in l,
                      "legacy HEL rejection")
        legacy.close()
        print("PASS: legacy HEL 4 without OCL token aborted the link")

        # ---------------------------------------------------------------
        # Test 2: HEL 4 OCL confirmed; local inventory replayed; OCLG INCOMPLETE
        # ---------------------------------------------------------------
        peer = MockPeer("127.0.0.1", server_port)
        peer.negotiate(ocl=True)
        logs.wait_for(lambda l: "UDB HEL 4 capability confirmed" in l, "HEL confirmed")
        peer.wait_for(lambda l: re.search(r" DB \* OCL BEGIN 001 [0-9a-f]{16} \d+ \d+ [0-9a-f]{64}", l),
                      "local OCL inventory replay")
        local_entries = peer.collect_ocl_items()
        assert ("udbtest" in [n for n, _ in local_entries]), f"udbtest missing from replay: {local_entries}"
        assert ("udbtest-child" in [n for n, _ in local_entries]), f"child missing from replay: {local_entries}"
        for _, dig in local_entries:
            assert HEX64.match(dig), f"bad digest {dig}"
        print(f"PASS: local OCL inventory replayed with {len(local_entries)} classes")

        peer.drain()
        oclg = peer.collect_oclg_snapshots()
        assert any(state == "INCOMPLETE" for _, state, _ in oclg), f"expected INCOMPLETE OCLG snapshot: {oclg}"
        print("PASS: OCLG snapshot INCOMPLETE while the peer has no inventory")

        # ---------------------------------------------------------------
        # Test 3: divergent committed inventory -> READY registry, empty OCLG
        # ---------------------------------------------------------------
        divergent = ("netadmin", "f" * 64)
        peer.send_ocl_snapshot("aa" * 8, 1, [divergent])
        logs.wait_for(lambda l: "Committed operclass inventory for origin 002: generation 1" in l,
                      "remote commit of divergent inventory")
        peer.drain()
        oclg = peer.collect_oclg_snapshots()
        assert any(state == "READY" and not entries for _, state, entries in oclg), \
            f"expected READY empty OCLG after divergent inventory: {oclg}"
        print("PASS: divergent inventory commits; OCLG READY with empty intersection")

        # ---------------------------------------------------------------
        # Test 4: matching inventory -> OCLG READY with both classes
        # ---------------------------------------------------------------
        peer.send_ocl_snapshot("bb" * 8, 2, local_entries)
        logs.wait_for(lambda l: "Committed operclass inventory for origin 002: generation 2" in l,
                      "remote commit of matching inventory")
        peer.drain()
        oclg = peer.collect_oclg_snapshots()
        ready_two = [snap for snap in oclg if snap[1] == "READY" and len(snap[2]) == len(local_entries)]
        assert ready_two, f"expected READY OCLG with {len(local_entries)} classes: {oclg}"
        names = sorted(n for n, _ in ready_two[-1][2])
        assert names == ["udbtest", "udbtest-child"], f"unexpected OCLG contents: {names}"
        print("PASS: matching inventory yields OCLG READY with the full intersection")

        # /UDB observability
        peer.send("UDB OPERCLASSES")
        peer.wait_for(lambda l: "Operclass registry: READY" in l, "registry READY via /UDB")
        peer.drain(0.5)
        assert any("Local inventory: generation" in l for l in peer.lines), "missing local inventory line"
        peer.send("UDB OPERCLASS udbtest")
        peer.wait_for(lambda l: "Operclass udbtest: GLOBAL" in l, "operclass GLOBAL via /UDB")
        print("PASS: /UDB OPERCLASSES and /UDB OPERCLASS report the global view")

        # ---------------------------------------------------------------
        # Test 5: stale generation is ignored
        # ---------------------------------------------------------------
        commits_before = log_count_containing(logs, "Committed operclass inventory")
        peer.send_ocl_snapshot("bb" * 8, 1, [divergent])
        time.sleep(0.5)
        logs.read_available()
        assert log_count_containing(logs, "Committed operclass inventory") == commits_before, \
            "stale generation was committed"
        print("PASS: stale generation ignored")

        # ---------------------------------------------------------------
        # Test 6: same generation with different digest is a protocol violation
        # ---------------------------------------------------------------
        bad_aggregate = ocl_inventory_digest([(divergent[0], divergent[1]), ("extra", "a" * 64)])
        peer.send_ocl_snapshot("bb" * 8, 2, [(divergent[0], divergent[1]), ("extra", "a" * 64)],
                               aggregate=bad_aggregate)
        logs.wait_for(lambda l: "re-advertised" in l, "protocol violation for same-generation")
        assert log_count_containing(logs, "Committed operclass inventory") == commits_before
        print("PASS: same epoch/generation with different digest rejected without replacing state")

        # ---------------------------------------------------------------
        STAGE_EPOCH = "dd" * 8
        # Test 7: duplicate ITEM aborts the stage
        # ---------------------------------------------------------------
        agg = ocl_inventory_digest(local_entries[:1])
        peer.send(f"DB * OCL BEGIN {PEER_SID} {STAGE_EPOCH} 3 1 {agg}")
        peer.send(f"DB * OCL ITEM {PEER_SID} {STAGE_EPOCH} 3 {local_entries[0][0]} {local_entries[0][1]}")
        peer.send(f"DB * OCL ITEM {PEER_SID} {STAGE_EPOCH} 3 {local_entries[0][0]} {local_entries[0][1]}")
        logs.wait_for(lambda l: "staging for origin" in l and "duplicate item" in l,
                      "duplicate item stage abort")
        print("PASS: duplicate ITEM aborted the stage")

        # ---------------------------------------------------------------
        # Test 8: END with item count mismatch aborts the stage
        # ---------------------------------------------------------------
        peer.send(f"DB * OCL BEGIN {PEER_SID} {STAGE_EPOCH} 4 1 {agg}")
        peer.send(f"DB * OCL END {PEER_SID} {STAGE_EPOCH} 4")
        logs.wait_for(lambda l: "item count mismatch" in l,
                      "count mismatch stage abort")
        print("PASS: END with wrong item count aborted the stage")

        # ---------------------------------------------------------------
        # Test 9: wrong aggregate digest aborts the stage and is not forwarded
        # ---------------------------------------------------------------
        wrong_agg = ("0" if agg[-1] != "0" else "1").join([agg[:-1], ""])
        peer.send(f"DB * OCL BEGIN {PEER_SID} {STAGE_EPOCH} 5 1 {wrong_agg}")
        peer.send(f"DB * OCL ITEM {PEER_SID} {STAGE_EPOCH} 5 {local_entries[0][0]} {local_entries[0][1]}")
        peer.send(f"DB * OCL END {PEER_SID} {STAGE_EPOCH} 5")
        logs.wait_for(lambda l: "inventory digest mismatch" in l,
                      "digest mismatch stage abort")
        print("PASS: END with incorrect inventory_digest aborted the stage")

        # ---------------------------------------------------------------
        # Test 10: recovery after aborts
        # ---------------------------------------------------------------
        peer.send_ocl_snapshot("ee" * 8, 6, local_entries)
        logs.wait_for(lambda l: "Committed operclass inventory for origin 002: generation 6" in l,
                      "recovery commit")
        print("PASS: registry recovers with a fresh valid snapshot after aborts")

        # ---------------------------------------------------------------
        # Test 11: origin quit -> registry INCOMPLETE and OCLG withdrawn
        # ---------------------------------------------------------------
        peer.close()
        logs.wait_for(lambda l: "Operclass registry incomplete" in l, "registry INCOMPLETE after origin quit")
        print("PASS: origin quit withdrew the registry and OCLG availability")

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB operclass registry integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
