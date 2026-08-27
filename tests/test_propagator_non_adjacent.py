#!/usr/bin/env python3
"""Integration test verifying that a non-adjacent propagator is never selected for staged sync.

Topology:
  Hub A <-> Relay B <-> Leaf C
  Cluster S::propagator = hub-a.test,hub-b.test

Prior to the fix:
  Leaf C searched the global IRC server list (find_server) and found 'hub-a.test' online.
  Because hub-a.test was 2 hops away, MyConnect(A) was false on C, so HEL 4 negotiation
  with Relay B sent 'hub-a.test'. Relay B rejected authorization (me.name == hub-b.test),
  causing RES and staged sync from Relay B to Leaf C to be blocked with ERR RES FORBIDDEN.

With the fix:
  Leaf C enforces hop-by-hop selection: hub-a.test is skipped because it is not a directly
  connected peer (MyConnect is false). Leaf C dynamically selects Relay B (hub-b.test).
  Relay B confirms authorization and cleanly syncs staged snapshots to Leaf C.
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

ROOT = pathlib.Path(__file__).resolve().parents[5]
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)


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


def write_config(path, name, sid, ports, links, dbdir, propagator=None, link_password=""):
    link_text = ""
    for peer, peer_port, autoconnect in links:
        outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                    'options { autoconnect; } }\n') if autoconnect else ""
        link_text += f'''link {peer} {{
    incoming {{ mask "*@*"; }}
{outgoing}    password "{link_password}";
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
    info "UDB non-adjacent propagator test node";
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


def main():
    ircd = DEFAULT_IRCD
    if not ircd.is_file():
        print(f"SKIP: unrealircd binary not found at {ircd}")
        return 77

    module = find_module_path()
    if not module.is_file():
        print(f"SKIP: udb.so not built at {module}")
        return 77

    temp_root = pathlib.Path(tempfile.mkdtemp(prefix="udb-nonadj-test-"))
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

        # Cluster configuration in S block: hub-a.test,hub-b.test
        propagator_list = "hub-a.test,hub-b.test"

        # Seed Node A (Root Propagator) with S block and latest N block
        (node_a / "data/udb_S.db").write_text(
            f"; UDB Block S\n; Saved: 1787720000\n; Records: 2\npropagator {propagator_list}\nflood 5:30\n",
            encoding="ascii"
        )
        (node_a / "data/udb_N.db").write_text(
            "; UDB Block N\n; Saved: 1787720000\n; Records: 2\nalice::vhost official.alice.net\nbob::vhost official.bob.net\n",
            encoding="ascii"
        )
        (node_a / "data/.udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")

        # Seed Node B (Relay) with S block
        (node_b / "data/udb_S.db").write_text(
            f"; UDB Block S\n; Saved: 1787720000\n; Records: 2\npropagator {propagator_list}\nflood 5:30\n",
            encoding="ascii"
        )

        # Seed Node C (Leaf) with PERSISTED S block BEFORE connecting to network!
        # This guarantees auto-bootstrap with '?' does not mask non-adjacent selection.
        (node_c / "data/udb_S.db").write_text(
            f"; UDB Block S\n; Saved: 1787720000\n; Records: 2\npropagator {propagator_list}\nflood 5:30\n",
            encoding="ascii"
        )
        # Node C starts with an older, outdated N.db
        (node_c / "data/udb_N.db").write_text(
            "; UDB Block N\n; Saved: 1787710000\n; Records: 1\nolduser::vhost outdated.vhost.net\n",
            encoding="ascii"
        )
        (node_c / "data/.udb_state").write_text("STATE=READY\nLAST_SYNC=1787710000\n", encoding="ascii")

        all_ports = free_ports(9)
        ports_a = tuple(all_ports[0:3])
        ports_b = tuple(all_ports[3:6])
        ports_c = tuple(all_ports[6:9])
        link_pw = secrets.token_hex(16)

        config_a = node_a / "unrealircd.conf"
        config_b = node_b / "unrealircd.conf"
        config_c = node_c / "unrealircd.conf"

        # Topology: A <-> B <-> C (A is not linked to C)
        write_config(config_a, "hub-a.test", "00A", ports_a,
                     [("hub-b.test", ports_b[1], False)],
                     node_a / "data", propagator=None, link_password=link_pw)

        write_config(config_b, "hub-b.test", "00B", ports_b,
                     [("hub-a.test", ports_a[1], True), ("leaf-c.test", ports_c[1], False)],
                     node_b / "data", propagator=None, link_password=link_pw)

        write_config(config_c, "leaf-c.test", "00C", ports_c,
                     [("hub-b.test", ports_b[1], True)],
                     node_c / "data", propagator=None, link_password=link_pw)

        for name, n, cfg, p0 in (("A", node_a, config_a, ports_a[0]),
                                 ("B", node_b, config_b, ports_b[0]),
                                 ("C", node_c, config_c, ports_c[0])):
            log_path = n / "ircd.log"
            logs[name] = log_path
            out_f = log_path.open("w")
            proc = subprocess.Popen(bwrap_command(n, ircd, cfg), stdout=out_f, stderr=subprocess.STDOUT)
            processes.append(proc)
            wait_for_daemon(proc, "127.0.0.1", p0)

        # Wait for 3-node network to link and staged sync to complete down to Leaf C
        n_file_c = node_c / "data/udb_N.db"
        deadline = time.monotonic() + 15
        reconciled = False

        while time.monotonic() < deadline:
            if n_file_c.exists():
                content = n_file_c.read_text(encoding="ascii", errors="replace")
                if "alice::vhost official.alice.net" in content and "bob::vhost official.bob.net" in content:
                    reconciled = True
                    break
            time.sleep(0.2)

        if not reconciled:
            raise AssertionError(f"FAIL: Leaf C failed to reconcile staged N block from Hub A via Relay B!\n"
                                 f"Content of C's N.db:\n{n_file_c.read_text(errors='replace') if n_file_c.exists() else 'NONE'}")

        content_c = n_file_c.read_text(encoding="ascii")
        assert "olduser::vhost" not in content_c, f"Stale record was not replaced on Leaf C: {content_c}"
        assert "alice::vhost official.alice.net" in content_c, f"alice missing on Leaf C: {content_c}"
        assert "bob::vhost official.bob.net" in content_c, f"bob missing on Leaf C: {content_c}"

        # Prove the selection preconditions instead of inferring them only from
        # the final file. C learns A through B, while its sole configured direct
        # server link is B. B's HEL log then records C advertising B, and C's
        # protocol log records a transaction targeted directly at C.
        config_c_text = config_c.read_text(encoding="ascii")
        log_a_text = logs["A"].read_text(errors="replace")
        log_b_text = logs["B"].read_text(errors="replace")
        log_c_text = logs["C"].read_text(errors="replace")
        assert "link hub-b.test" in config_c_text
        assert "link hub-a.test" not in config_c_text
        assert (
            "hub-a.test" in log_c_text
        ), "C never observed global server A"
        assert "Direct peer selected hub-b.test as its staged-sync source" in log_b_text, \
            "C did not advertise direct peer B as its selected source"
        for subcmd in ("BEGIN", "PUT", "END"):
            assert f"target=00C subcmd={subcmd}" in log_c_text, f"C did not receive direct staged {subcmd}"
            assert f"target=00C subcmd={subcmd}" not in log_a_text, f"A processed a staged {subcmd} for non-adjacent C"

        print("PASS: Leaf C successfully selected directly linked Relay B over non-adjacent Hub A!")
        print("PASS: Leaf C received and committed staged snapshot from Relay B, resolving outdated N.db!")
        print("ALL TESTS PASSED: Non-adjacent propagator selection bug verified fixed.")
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
