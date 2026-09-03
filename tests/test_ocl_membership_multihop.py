#!/usr/bin/env python3
"""Real A-B-C/D topology test for OCL membership cleanup on a netsplit."""

import os
import pathlib
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
MODULE_NAME = "third/udb"
PASSWORD = "udb-ocl-multihop-password"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)


def free_ports(count):
    sockets = []
    ports = []
    for _ in range(count):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    for sock in sockets:
        sock.close()
    return ports


def write_config(path, name, sid, ports, links, dbdir, divergent=False):
    link_text = ""
    for peer, peer_port, autoconnect in links:
        outgoing = ""
        if autoconnect:
            outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; '
                        f"port {peer_port}; options {{ autoconnect; }} }}\n")
        link_text += f'''link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
{outgoing}    password "{PASSWORD}";
    class servers;
}}
'''
    channel_acl = "operonly;" if divergent else "oper { }"
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB OCL membership multihop test";
    sid "{sid}";
}}
admin {{ "UDB harness"; "udb"; "udb@example.invalid"; }}
set {{
    kline-address "udb@example.invalid";
    default-server "{name}";
    network-name "UDB OCL Harness";
    help-channel "#help";
    cloak-keys {{ "{CLOAK_KEYS[0]}"; "{CLOAK_KEYS[1]}"; "{CLOAK_KEYS[2]}"; }}
}}
class clients {{ pingfreq 60; maxclients 20; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 10; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {ports[0]}; }}
listen {{ ip "127.0.0.1"; port {ports[1]}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {ports[2]}; options {{ tls; }} }}
{link_text}operclass udbtest {{
    permissions {{
        channel {{ {channel_acl} }}
    }}
}}
oper udbtest-oper {{
    mask "*@*";
    password "udbtest-pass";
    operclass "udbtest";
    class clients;
}}
loadmodule "cloak_sha256";
loadmodule "{MODULE_NAME}";
udb {{ database-directory "{dbdir}"; }}
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
            process.wait(timeout=5)


def wait_until(predicate, description, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(f"Timeout waiting for {description}")


class MockClient:
    def __init__(self, host, port, nick):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"NICK {nick}")
        self.send(f"USER {nick} 0 * :OCL test client")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

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
        while time.monotonic() < deadline:
            for line in self.lines:
                if predicate(line):
                    return line
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}: {self.lines}")

    def close(self):
        self.sock.close()


def query_operclass(client_port, name):
    client = MockClient("127.0.0.1", client_port, "ocl-query")
    try:
        client.wait_for(lambda line: " 001 " in line, "client registration")
        client.send("OPER udbtest-oper udbtest-pass")
        client.wait_for(lambda line: " 381 " in line, "operator login")
        client.send(f"UDB OPERCLASS {name}")
        client.wait_for(lambda line: f"Operclass {name}:" in line, "operclass result")
        return client.lines[:]
    finally:
        client.close()


def run_tests(ircd, module, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-ocl-membership-"))
    processes = []
    logs = {}
    try:
        names = {"A": "ocl-a.test", "B": "ocl-b.test", "C": "ocl-c.test", "D": "ocl-d.test"}
        sids = {"A": "0A1", "B": "0B1", "C": "0C1", "D": "0D1"}
        nodes = {label: tmpdir / f"node-{label.lower()}" for label in names}
        ports = {label: tuple(free_ports(3)) for label in names}
        for label, node in nodes.items():
            (node / "data").mkdir(parents=True)
            (node / "modules" / "third").mkdir(parents=True)
            shutil.copy2(module, node / "modules" / "third" / "udb.so")
            links = []
            if label == "A":
                links = [(names["B"], ports["B"][1], True)]
            elif label == "B":
                links = [(names["A"], ports["A"][1], False),
                         (names["C"], ports["C"][1], True),
                         (names["D"], ports["D"][1], True)]
            else:
                links = [(names["B"], ports["B"][1], False)]
            config = node / "unrealircd.conf"
            write_config(config, names[label], sids[label], ports[label], links,
                         node / "data", divergent=label == "D")
            logs[label] = node / "ircd.log"

        def start(label):
            output = logs[label].open("w")
            processes.append(subprocess.Popen(bwrap_command(nodes[label], ircd,
                                                              nodes[label] / "unrealircd.conf"),
                                               stdout=output, stderr=subprocess.STDOUT))

        start("B")
        start("A")
        start("C")
        start("D")
        wait_until(lambda: all(logs[label].exists() and
                                "Committed operclass inventory" in logs[label].read_text(errors="replace")
                                for label in ("A", "B", "C", "D")),
                   "initial OCL commits", timeout=25)
        wait_until(lambda: all(f"origin {sids[label]}" in logs["A"].read_text(errors="replace")
                                for label in ("B", "C", "D")),
                   "A receiving B, C and D inventories", timeout=15)

        before = query_operclass(ports["A"][0], "udbtest")
        assert any("Operclass udbtest: NOT GLOBAL" in line for line in before), before
        assert any(f"{sids[label]} MISMATCH" in line for label in ("B", "C", "D") for line in before), before
        print("PASS: A sees B, C and D as current OCL members and detects divergence")

        stop(processes[0])  # B is the root of the real split; C and D are dependants.
        wait_until(lambda: "UDB_OCL_REGISTRY_READY" in logs["A"].read_text(errors="replace") or
                   "Operclass udbtest entered the global" in logs["A"].read_text(errors="replace"),
                   "A recomputing after B-C-D split", timeout=15)
        after = query_operclass(ports["A"][0], "udbtest")
        assert any("Operclass udbtest: GLOBAL" in line for line in after), after
        assert not any(sids[label] in line for label in ("B", "C", "D") for line in after), after
        print("PASS: netsplit purged B/C/D membership and restored GLOBAL on A")
    finally:
        for process in processes:
            stop(process)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    ircd = pathlib.Path(os.environ.get("UNREALIRCD_BIN", DEFAULT_IRCD))
    module = pathlib.Path(os.environ.get("UDB_MODULE_PATH", RUNTIME_ROOT / "modules/third/udb.so"))
    if not ircd.is_file():
        print(f"SKIP: UnrealIRCd binary not found at {ircd}")
        sys.exit(77)
    if not module.is_file():
        print(f"SKIP: UDB module not found at {module}")
        sys.exit(77)
    run_tests(ircd, module, keep="--keep" in sys.argv)
