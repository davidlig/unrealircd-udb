#!/usr/bin/env python3
"""Strict validation tests for udb::propagator (unrealircd.conf) and S::propagator (Block S)."""

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
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
LINK_PASSWORD = "testlinkpassword"
IRCD_SID = "00A"


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
    info "UDB validation test node";
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


def bwrap_command(node, ircd, config, configtest=False):
    for sub in ("runtime-data", "tmp", "cache", "logs"):
        (node / sub).mkdir(parents=True, exist_ok=True)
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
               "--bind", str(node), str(node),
               "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
               "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
               "--bind", str(node / "cache"), str(RUNTIME_ROOT / "cache"),
               "--bind", str(node / "logs"), str(RUNTIME_ROOT / "logs"),
               "--ro-bind", str(node / "modules" / "third"), str(RUNTIME_ROOT / "modules/third"),
               "--dev-bind", "/dev", "/dev", "--proc", "/proc",
               str(ircd), "-f", str(config)]
    command.append("-c" if configtest else "-F")
    return command


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


def run_configtest(node, ircd, config):
    proc = subprocess.run(
        bwrap_command(node, ircd, config, configtest=True),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return proc.returncode == 0, proc.stdout + proc.stderr


class MockPeer:
    def __init__(self, name, sid, host, port, propagator_advertised):
        self.name = name
        self.sid = sid
        self.ircd_sid = IRCD_SID
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
        self.send(f"DB {self.ircd_sid} HEL 4 {propagator_advertised} OCL")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, f"{self.name} HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK OCL")

    def send_raw(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("utf-8"))

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

    def wait_for(self, predicate, description, timeout=5, start_idx=0):
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

    temp_root = pathlib.Path(tempfile.mkdtemp(prefix="udb-prop-val-test-"))
    processes = []
    log_file = None

    try:
        node = temp_root / "node"
        (node / "data").mkdir(parents=True)
        (node / "runtime-data").mkdir(parents=True)
        (node / "tmp").mkdir(parents=True)
        (node / "modules/third").mkdir(parents=True)
        shutil.copy2(module, node / "modules/third/udb.so")

        ports = free_ports(3)
        config_path = node / "unrealircd.conf"

        # -------------------------------------------------------------
        # Part 1: CONFIGTEST validation for udb::propagator
        # -------------------------------------------------------------
        invalid_propagator_cfgs = [
            "",
            "   ",
            " leading.example.net",
            "trailing.example.net ",
            "host with spaces.net",
            "host\twith\ttabs.net",
            "host\rwith\rCR.net",
            "host\nwith\nLF.net",
            "a" * 60 + ".net",  # 64 chars > HOSTLEN (63)
            "invalid!host.net",
            "bad@domain.com",
            "srv:6667",
            "foo/bar",
            "nodot",
        ]

        for inv in invalid_propagator_cfgs:
            write_config(config_path, "test.hub.net", IRCD_SID, ports, [], node / "data", propagator=inv)
            passed, out = run_configtest(node, ircd, config_path)
            assert not passed, f"Configtest should fail for invalid udb::propagator: {inv!r}, but passed! Output:\n{out}"
            print(f"PASS: configtest rejected invalid udb::propagator: {inv!r}")

        valid_propagator_cfgs = [
            "hub.example.net",
            "hub-1.example.org",
            "srv_01.irc.net",
            "services.local",
            "a" * 59 + ".net",  # 63 chars == HOSTLEN
        ]

        for val in valid_propagator_cfgs:
            write_config(config_path, "test.hub.net", IRCD_SID, ports, [], node / "data", propagator=val)
            passed, out = run_configtest(node, ircd, config_path)
            assert passed, f"Configtest should succeed for valid udb::propagator: {val!r}, but failed! Output:\n{out}"
            print(f"PASS: configtest accepted valid udb::propagator: {val!r}")

        # -------------------------------------------------------------
        # Part 2: Runtime S2S validation for S::propagator
        # -------------------------------------------------------------
        links = [("peer.net", 0, False)]
        write_config(config_path, "test.hub.net", IRCD_SID, ports, links, node / "data", propagator="peer.net")
        log_file = node / "ircd.log"
        out_f = log_file.open("w")
        proc = subprocess.Popen(bwrap_command(node, ircd, config_path), stdout=out_f, stderr=subprocess.STDOUT)
        processes.append(proc)
        wait_for_daemon(proc, "127.0.0.1", ports[0])

        peer = MockPeer("peer.net", "00B", "127.0.0.1", ports[1], "peer.net")

        invalid_s_propagators = [
            "",
            ",a.net",
            "a.net,",
            "a.net,,b.net",
            "a.net,   ,b.net",
            "a" * 60 + ".net",  # 64 chars > HOSTLEN (63)
            "a.net," + "b" * 60 + ".net",
            "host\rwith\rCR.net",
            "host\nwith\nLF.net",
            "host\twith\ttab.net",
            "invalid!host.net",
            "bad@domain.com,valid.net",
            "srv:6667,hub.net",
            "nodot",
        ]

        for inv in invalid_s_propagators:
            start_idx = len(peer.lines)
            peer.send(f"DB {IRCD_SID} INS S::propagator :{inv}")
            # Expect ERR INS
            peer.wait_for(lambda line: " DB " in line and " ERR INS " in line, f"ERR INS for {inv!r}",
                          start_idx=start_idx)
            print(f"PASS: S2S INS rejected invalid S::propagator: {inv!r}")

        valid_s_propagators = [
            "a.example.net",
            "a.example.net,b.example.net",
            "  a.example.net  ,  b.example.net  ",
        ]

        tokens_512 = [f"srv-{i:03d}.example.net" for i in range(35)]
        list_512 = ",".join(tokens_512)
        assert len(list_512) > 550
        valid_s_propagators.append(list_512)

        tokens_4k = []
        while True:
            candidate = f"long-node-{len(tokens_4k):04d}.example.net"
            proposed = ",".join((*tokens_4k, candidate))
            if len(proposed) > 4096:
                break
            tokens_4k.append(candidate)
        list_4k = ",".join(tokens_4k)
        assert 4000 <= len(list_4k) <= 4096
        valid_s_propagators.append(list_4k)

        for val in valid_s_propagators:
            start_idx = len(peer.lines)
            peer.send(f"DB {IRCD_SID} INS S::propagator :{val}")
            time.sleep(0.1)
            peer.receive(time.monotonic() + 0.2)
            recent = peer.lines[start_idx:]
            for line in recent:
                assert "ERR INS" not in line, f"Unexpected ERR INS for valid S::propagator of len {len(val)}: {line}"
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                db_text = (node / "data/udb_S.db").read_text(errors="replace")
                if f"propagator {val}" in db_text:
                    break
                time.sleep(0.05)
            else:
                raise AssertionError(f"Valid S::propagator was not persisted exactly (len={len(val)})")
            print(f"PASS: S2S INS accepted valid S::propagator (len={len(val)})")

        peer.close()
        print("ALL TESTS PASSED: udb::propagator and S::propagator strict validation verified successfully.")
        return 0

    finally:
        for p in processes:
            stop(p)
        if sys.exc_info()[0] is not None and log_file and log_file.exists():
            print(f"--- Server Log ---\n{log_file.read_text(errors='replace')}", file=sys.stderr)
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
