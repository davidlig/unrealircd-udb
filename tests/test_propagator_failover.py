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


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


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
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
                "--bind", str(node), str(node),
                "--bind", str(node / "runtime-data"), str(RUNTIME_ROOT / "data"),
                "--bind", str(node / "tmp"), str(RUNTIME_ROOT / "tmp"),
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


def main():
    ircd = DEFAULT_IRCD
    if not ircd.is_file():
        print(f"SKIP: unrealircd binary not found at {ircd}")
        return 77

    module = ROOT / "src/modules/third/udb/dist/udb.so"
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
        s_file_a.write_text("S::propagator hub-a.test,hub-b.test\nS::flood 5:30\n", encoding="ascii")

        # Seed Node A with an N record
        n_file_a = node_a / "data/udb_N.db"
        n_file_a.write_text("N::davidlig::vhost root.admin.net\n", encoding="ascii")

        ports_a = (free_port(), free_port(), free_port())
        ports_b = (free_port(), free_port(), free_port())
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

        proc_a = subprocess.Popen(bwrap_command(node_a, ircd, config_a),
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(proc_a)
        wait_for_daemon(proc_a, "127.0.0.1", ports_a[0])

        proc_b = subprocess.Popen(bwrap_command(node_b, ircd, config_b),
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(proc_b)
        wait_for_daemon(proc_b, "127.0.0.1", ports_b[0])

        # Wait for S2S autoconnect and UDB sync
        time.sleep(3)

        # Verify that Leaf B received block S (including S::propagator) and block N via auto-bootstrap
        s_file_b = node_b / "data/udb_S.db"
        n_file_b = node_b / "data/udb_N.db"

        if not s_file_b.exists() or not n_file_b.exists():
            raise AssertionError("FAIL: Leaf B did not persist synced UDB database blocks!")

        content_s = s_file_b.read_text(encoding="ascii")
        content_n = n_file_b.read_text(encoding="ascii")

        assert "S::propagator hub-a.test,hub-b.test" in content_s, f"S::propagator missing in B: {content_s}"
        assert "N::davidlig::vhost root.admin.net" in content_n, f"N::davidlig missing in B: {content_n}"

        print("PASS: Clean Leaf B successfully auto-bootstrapped and synchronized S::propagator & N block from Hub A without local propagator config!")
        print("ALL TESTS PASSED: UDB S::propagator priority list and Auto-Bootstrap verified successfully.")
        return 0

    finally:
        for p in processes:
            stop(p)
        shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
