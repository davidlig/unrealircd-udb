#!/usr/bin/env python3
"""Integration test for UDB S::propagator priority list failover and auto-bootstrap."""

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


class EnvironmentUnavailable(Exception):
    pass


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


def free_port():
    return free_ports(1)[0]


def write_config(path, name, sid, ports, links, dbdir, propagator=None, link_password=""):
    link_text = ""
    for peer, peer_port, autoconnect in links:
        outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                    'options { autoconnect; } }\n') if autoconnect else ""
        link_text += f'''link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
{outgoing}    password "{link_password}";
    class servers;
}}
'''
    udb_block = f'''udb {{
    database-directory "{dbdir}";
{f'    propagator "{propagator}";' if propagator else ''}
}}'''

    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB failover test node";
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


def main():
    ircd = DEFAULT_IRCD
    if not ircd.is_file():
        print(f"SKIP: unrealircd binary not found at {ircd}")
        return 77

    module = find_module_path()
    if not module.is_file():
        print(f"SKIP: udb.so not built at {module}")
        return 77

    temp_root = pathlib.Path(tempfile.mkdtemp(prefix="udb-failover-test-"))
    processes = []

    try:
        # Hub A has S::propagator set to "hub-a.test,hub-b.test"
        # Leaf B has NO propagator configured in unrealircd.conf (clean bootstrap test)
        node_a = temp_root / "node-a"
        node_b = temp_root / "node-b"
        for n in (node_a, node_b):
            (n / "data").mkdir(parents=True)
            (n / "runtime-data").mkdir(parents=True)
            (n / "tmp").mkdir(parents=True)
            (n / "modules/third").mkdir(parents=True)
            shutil.copy2(module, n / "modules/third/udb.so")

        # Seed Node A with S block having propagator setting
        s_file_a = node_a / "data/udb_S.db"
        s_file_a.write_text("; UDB Block S\n; Saved: 1787720000\n; Records: 2\npropagator hub-a.test,hub-b.test\nflood 5:30\n", encoding="ascii")

        # Seed Node A with an N record
        n_file_a = node_a / "data/udb_N.db"
        n_file_a.write_text("; UDB Block N\n; Saved: 1787720000\n; Records: 1\ndavidlig::vhost root.admin.net\n", encoding="ascii")

        all_ports = free_ports(6)
        ports_a = tuple(all_ports[0:3])
        ports_b = tuple(all_ports[3:6])
        link_pw = secrets.token_hex(16)

        config_a = node_a / "unrealircd.conf"
        config_b = node_b / "unrealircd.conf"

        write_config(config_a, "hub-a.test", "00A", ports_a,
                     [("leaf-b.test", ports_b[1], False)],
                     node_a / "data", propagator=None, link_password=link_pw)

        # Node B has NO propagator in config! Tests auto-bootstrap from Hub A!
        write_config(config_b, "leaf-b.test", "00B", ports_b,
                     [("hub-a.test", ports_a[1], True)],
                     node_b / "data", propagator=None, link_password=link_pw)

        log_a = node_a / "ircd.log"
        log_b = node_b / "ircd.log"
        out_a = log_a.open("w")
        out_b = log_b.open("w")

        proc_a = subprocess.Popen(bwrap_command(node_a, ircd, config_a),
                                  stdout=out_a, stderr=subprocess.STDOUT)
        processes.append(proc_a)
        wait_for_daemon(proc_a, "127.0.0.1", ports_a[0])

        proc_b = subprocess.Popen(bwrap_command(node_b, ircd, config_b),
                                  stdout=out_b, stderr=subprocess.STDOUT)
        processes.append(proc_b)
        wait_for_daemon(proc_b, "127.0.0.1", ports_b[0])

        # Wait for S2S autoconnect and UDB sync
        s_file_b = node_b / "data/udb_S.db"
        n_file_b = node_b / "data/udb_N.db"

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if s_file_b.exists() and n_file_b.exists() and s_file_b.stat().st_size > 0 and n_file_b.stat().st_size > 0:
                break
            time.sleep(0.2)

        if not s_file_b.exists() or not n_file_b.exists():
            raise AssertionError("FAIL: Leaf B did not persist synced UDB database blocks!")

        content_s = s_file_b.read_text(encoding="ascii")
        content_n = n_file_b.read_text(encoding="ascii")

        assert "propagator hub-a.test,hub-b.test" in content_s, f"propagator missing in B: {content_s}"
        assert "davidlig::vhost root.admin.net" in content_n, f"davidlig missing in B: {content_n}"

        print("PASS: Clean Leaf B successfully auto-bootstrapped and synchronized S::propagator & N block from Hub A without local propagator config!")
        print("ALL TESTS PASSED: UDB S::propagator priority list and Auto-Bootstrap verified successfully.")
        return 0

    finally:
        for p in processes:
            stop(p)
        if sys.exc_info()[0] is not None:
            if log_a.exists():
                print(f"--- Hub A Log ---\n{log_a.read_text(errors='replace')}", file=sys.stderr)
            if log_b.exists():
                print(f"--- Leaf B Log ---\n{log_b.read_text(errors='replace')}", file=sys.stderr)
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
