#!/usr/bin/env python3
"""Real A-B-Services coverage for HEL instance recovery and OCL roles."""

import os
import pathlib
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
PASSWORD = "udb-ocl-rehash-password"
SERVICE_NAME = "udb-services.test"
SERVICE_SID = "00S"
SERVICE_EPOCH = "3333333333333333"


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


def write_config(path, name, sid, ports, module, dbdir, peer_name=None, peer_port=None,
                 service=False, changed=False):
    links = ""
    if peer_name:
        outgoing = ""
        if peer_port:
            outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; '
                        f"port {peer_port}; options {{ autoconnect; }} }}\n")
        links += f'''link {peer_name} {{
    incoming {{ mask "127.0.0.1"; }}
{outgoing}    password "{PASSWORD}";
    class servers;
}}
'''
    if service:
        links += f'''link {SERVICE_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{PASSWORD}";
    class servers;
}}
ulines {{
    {SERVICE_NAME};
}}
'''
    channel_permission = "operonly;" if changed else "oper { }"
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB OCL rehash harness";
    sid "{sid}";
}}
admin {{ "UDB harness"; "udb"; "udb@example.invalid"; }}
set {{
    kline-address "udb@example.invalid";
    default-server "{name}";
    network-name "UDB OCL Rehash";
    help-channel "#help";
    cloak-keys {{ "{CLOAK_KEYS[0]}"; "{CLOAK_KEYS[1]}"; "{CLOAK_KEYS[2]}"; }}
}}
class clients {{ pingfreq 60; maxclients 20; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 10; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {ports[0]}; }}
listen {{ ip "127.0.0.1"; port {ports[1]}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {ports[2]}; options {{ tls; }} }}
{links}operclass udbtest {{
    permissions {{ channel {{ {channel_permission} }} server {{ rehash {{ local; }} }} }}
}}
oper udbtest-oper {{
    mask "*@*";
    password "udbtest-pass";
    operclass "udbtest";
    class clients;
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
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


def wait_for_file(path, predicate, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = path.read_text(errors="replace") if path.exists() else ""
        if predicate(text):
            return text
        time.sleep(0.1)
    raise AssertionError(f"Timeout waiting for {description}: {path.read_text(errors='replace') if path.exists() else ''}")


class ServicePeer:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{PASSWORD}")
        self.send(f"PROTOCTL EAUTH={SERVICE_NAME}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + SERVICE_SID)
        self.send(f"SERVER {SERVICE_NAME} 1 :UDB test services")
        self.wait_for(lambda line: " 001 " in line or " NETINFO" in line, "server handshake")
        self.send("EOS")
        self.send(f"DB 001 HEL 4 ? {SERVICE_EPOCH} OCL OCLG")
        self.wait_for(lambda line: " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB 001 HEL 4 ACK ? {SERVICE_EPOCH} OCL OCLG")

    def send(self, command):
        if not command.startswith(":"):
            command = f":{SERVICE_SID} {command}"
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
                    if re.search(r":001 DB 00S HEL 4 \S+ [0-9a-f]{16} OCL$", line):
                        self.send(f"DB 001 HEL 4 ACK ? {SERVICE_EPOCH} OCL OCLG")

    def wait_for(self, predicate, description, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(predicate(line) for line in self.lines):
                return
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}: {self.lines}")

    def snapshots(self):
        snapshots = []
        current = None
        for line in self.lines:
            match = re.search(r" OCLG BEGIN ([0-9a-f]{16}) (\d+) (READY|INCOMPLETE) (\d+) ([0-9a-f]{64})", line)
            if match:
                current = (match.group(1), match.group(3), [])
                continue
            if current is not None:
                item = re.search(r" OCLG ITEM [0-9a-f]{16} \d+ (\S+) ([0-9a-f]{64})", line)
                if item:
                    current[2].append((item.group(1), item.group(2)))
                elif re.search(r" OCLG END [0-9a-f]{16} \d+", line):
                    snapshots.append(current)
                    current = None
        return snapshots

    def close(self):
        try:
            self.send("SQUIT " + SERVICE_NAME + " :bye")
        except OSError:
            pass
        self.sock.close()


class Client:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send("NICK rehash-client")
        self.send("USER rehash-client 0 * :rehash client")

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

    def wait_for(self, predicate, description, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(predicate(line) for line in self.lines):
                return
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}: {self.lines}")

    def close(self):
        self.sock.close()


def run_tests(ircd, module):
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-ocl-rehash-"))
    processes = []
    service = None
    client = None
    try:
        node_a = root / "node-a"
        node_b = root / "node-b"
        for node in (node_a, node_b):
            (node / "data").mkdir(parents=True)
            (node / "modules" / "third").mkdir(parents=True)
            shutil.copy2(module, node / "modules" / "third" / "udb.so")
        ports_a = free_ports(3)
        ports_b = free_ports(3)
        conf_a = node_a / "unrealircd.conf"
        conf_b = node_b / "unrealircd.conf"
        write_config(conf_a, "udb-a.test", "001", ports_a, module, node_a / "data",
                     peer_name="udb-b.test", peer_port=ports_b[1], service=True)
        write_config(conf_b, "udb-b.test", "002", ports_b, module, node_b / "data",
                     peer_name="udb-a.test")
        log_a = node_a / "ircd.log"
        log_b = node_b / "ircd.log"
        with log_b.open("w") as output_b:
            processes.append(subprocess.Popen(bwrap_command(node_b, ircd, conf_b), stdout=output_b,
                                              stderr=subprocess.STDOUT))
        with log_a.open("w") as output_a:
            processes.append(subprocess.Popen(bwrap_command(node_a, ircd, conf_a), stdout=output_a,
                                              stderr=subprocess.STDOUT))
        wait_for_file(log_a, lambda text: "origin 002" in text and "UDB HEL 4 capability confirmed" in text,
                      "A-B HEL and OCL convergence", timeout=30)
        wait_for_file(log_b, lambda text: "origin 001" in text and "UDB HEL 4 capability confirmed" in text,
                      "B-A HEL and OCL convergence", timeout=30)

        service = ServicePeer("127.0.0.1", ports_a[1])
        service.wait_for(lambda line: " OCLG BEGIN " in line and " READY " in line, "initial READY OCLG")
        assert not any(" OCL BEGIN " in line for line in service.lines), service.lines
        print("PASS: ULine Services received OCLG but no OCL inventory")

        client = Client("127.0.0.1", ports_a[0])
        client.wait_for(lambda line: " 001 " in line, "operator client registration")
        client.send("OPER udbtest-oper udbtest-pass")
        client.wait_for(lambda line: " 381 " in line, "operator login")
        initial_links = log_a.read_text(errors="replace").count("Server linked:")
        initial_snapshots = len(service.snapshots())

        client.send("REHASH")
        client.wait_for(lambda line: " 219 " in line or "Rehashing" in line, "unchanged REHASH", timeout=15)
        wait_for_file(log_b, lambda text: text.count("origin 001") >= 2, "automatic replay after unchanged REHASH", timeout=20)
        service.wait_for(lambda line: len(service.snapshots()) > initial_snapshots,
                         "OCLG after unchanged REHASH", timeout=15)
        assert log_a.read_text(errors="replace").count("Server linked:") == initial_links, "SERVER link reconnected"
        assert len(service.snapshots()) > initial_snapshots, "unchanged REHASH did not rebuild OCLG"
        assert not any(" OCL BEGIN " in line for line in service.lines), service.lines
        print("PASS: unchanged REHASH replayed automatically without SERVER reconnect or OCL leakage")

        write_config(conf_a, "udb-a.test", "001", ports_a, module, node_a / "data",
                     peer_name="udb-b.test", peer_port=ports_b[1], service=True, changed=True)
        before_changed = len(service.snapshots())
        client.send("REHASH")
        client.wait_for(lambda line: " 219 " in line or "Rehashing" in line, "changed REHASH", timeout=15)
        wait_for_file(log_b, lambda text: text.count("origin 001") >= 3, "automatic replay after changed REHASH", timeout=20)
        service.wait_for(lambda line: len(service.snapshots()) > before_changed,
                         "OCLG after changed REHASH", timeout=15)
        changed = service.snapshots()[before_changed:]
        assert changed and changed[-1][1] == "READY" and not changed[-1][2], changed
        assert log_a.read_text(errors="replace").count("Server linked:") == initial_links, "SERVER link reconnected"
        assert not any(" OCL BEGIN " in line for line in service.lines), service.lines
        print("PASS: changed REHASH withdrew the divergent class and rebuilt an empty READY OCLG")
        return 0
    finally:
        if client:
            client.close()
        if service:
            service.close()
        for process in processes:
            stop(process)
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    ircd = pathlib.Path(os.environ.get("UNREALIRCD_BIN", DEFAULT_IRCD))
    module = pathlib.Path(os.environ.get("UDB_MODULE_PATH", pathlib.Path(__file__).resolve().parents[1] / "src" / "udb.so"))
    if not ircd.is_file() or not module.is_file() or not shutil.which("bwrap"):
        print("SKIP: UnrealIRCd, UDB module, and bubblewrap are required")
        sys.exit(77)
    try:
        sys.exit(run_tests(ircd, module))
    except (OSError, subprocess.SubprocessError, AssertionError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
