#!/usr/bin/env python3
"""End-to-end integration tests for multi-hop live mutations across A-B-C network."""

import argparse
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


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
RUNTIME_ROOT = pathlib.Path(os.environ.get("UDB_TEST_IRCD_ROOT", pathlib.Path.home() / "unrealircd"))
DEFAULT_IRCD = RUNTIME_ROOT / "bin/unrealircd"
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)

MUTATOR_SOURCE = pathlib.Path(__file__).resolve().parent / "udb_test_mutator.c"
MUTATOR_MODULE = MUTATOR_SOURCE.with_suffix(".so")


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    local_path = REPO_ROOT / "src" / "udb.so"
    if local_path.is_file():
        return local_path
    runtime_path = RUNTIME_ROOT / "modules/third/udb.so"
    if runtime_path.is_file():
        return runtime_path
    return local_path


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


def free_port():
    return free_ports(1)[0]


def write_config(path, name, sid, ports, links, module, dbdir, propagator, link_password, load_mutator=False):
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
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB multi-hop integration node";
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
{('loadmodule "third/udb_test_mutator";' if load_mutator else '')}
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
            "--setenv", "UDB_TEST_MUTATOR_DIRECTORY", str(node / "data"),
            str(ircd), "-F", "-f", str(config)]


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wait_for_links(processes, targets, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(p.poll() is not None for p in processes):
            return False
        all_synced = True
        for log, expected_links in targets:
            if not log.exists():
                all_synced = False
                break
            text = log.read_text(errors="replace")
            if text.count("is now synced") < expected_links:
                all_synced = False
                break
        if all_synced:
            return True
        time.sleep(0.1)
    return False


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-multihop-"))
    module_path = find_module_path()
    processes = []

    try:
        a, b, c = tmpdir / "node-a", tmpdir / "node-b", tmpdir / "node-c"
        for node in (a, b, c):
            (node / "data").mkdir(parents=True)
            (node / "runtime-data").mkdir()
            (node / "tmp").mkdir()
            third_modules = node / "modules" / "third"
            third_modules.mkdir(parents=True)
            shutil.copy2(module_path, third_modules / "udb.so")
            if MUTATOR_MODULE.exists():
                shutil.copy2(MUTATOR_MODULE, third_modules / "udb_test_mutator.so")

        raw_ports = free_ports(9)
        ports = [tuple(raw_ports[i * 3:(i + 1) * 3]) for i in range(3)]
        a_conf, b_conf, c_conf = a / "unrealircd.conf", b / "unrealircd.conf", c / "unrealircd.conf"
        link_password = "udb-multihop-" + secrets.token_hex(16)

        # Topology: A <-> B <-> C. A is root propagator.
        write_config(a_conf, "udb-a.test", "0A1", ports[0],
                     (("udb-b.test", ports[1][1], True),),
                     module_path, a / "data", "udb-a.test", link_password, load_mutator=True)
        write_config(b_conf, "udb-b.test", "0B1", ports[1],
                     (("udb-a.test", ports[0][1], False), ("udb-c.test", ports[2][1], True)),
                     module_path, b / "data", "udb-a.test", link_password, load_mutator=False)
        write_config(c_conf, "udb-c.test", "0C1", ports[2],
                     (("udb-b.test", ports[1][1], False),),
                     module_path, c / "data", "udb-b.test", link_password, load_mutator=False)

        logs = {"A": a / "ircd.log", "B": b / "ircd.log", "C": c / "ircd.log"}

        def start(label, node, conf):
            with logs[label].open("w") as output:
                processes.append(subprocess.Popen(bwrap_command(node, ircd_bin, conf),
                                                  stdout=output, stderr=subprocess.STDOUT, text=True))

        start("B", b, b_conf)
        start("A", a, a_conf)
        if not wait_for_links(processes, ((logs["A"], 1), (logs["B"], 1)), 15):
            raise AssertionError("A-B link failed to form in time")

        start("C", c, c_conf)
        if not wait_for_links(processes, ((logs["B"], 2), (logs["C"], 1)), 15):
            raise AssertionError("B-C link failed to form in time")

        print("PASS: A-B-C network established and synchronized successfully")

        # Arm and trigger authorized INS at Node A
        time.sleep(1.0)
        (a / "data" / "udb-test-mutator-ins-go").touch()

        deadline = time.monotonic() + 10
        c_db_n = c / "data" / "udb_N.db"
        b_db_n = b / "data" / "udb_N.db"

        # Check that mutation was applied and persisted across B and C
        while time.monotonic() < deadline:
            b_text = b_db_n.read_text(errors="replace") if b_db_n.exists() else ""
            c_text = c_db_n.read_text(errors="replace") if c_db_n.exists() else ""
            if "udb-test-mutator" in b_text and "udb-test-mutator" in c_text:
                break
            time.sleep(0.2)

        b_text = b_db_n.read_text(errors="replace") if b_db_n.exists() else ""
        c_text = c_db_n.read_text(errors="replace") if c_db_n.exists() else ""

        if "udb-test-mutator" not in b_text:
            raise AssertionError(f"Node B did not apply and persist the A->B INS:\n{b_text}")
        if "udb-test-mutator" not in c_text:
            raise AssertionError(f"Node C did not apply and persist the multi-hop A->B->C INS:\n{c_text}")

        print("PASS: INS mutation propagated multi-hop A -> B -> C and persisted to disk across all nodes")

        # Arm and trigger authorized DEL at Node A
        (a / "data" / "udb-test-mutator-del-go").touch()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            b_text = b_db_n.read_text(errors="replace") if b_db_n.exists() else ""
            c_text = c_db_n.read_text(errors="replace") if c_db_n.exists() else ""
            if "udb-test-mutator" not in b_text and "udb-test-mutator" not in c_text:
                break
            time.sleep(0.2)

        b_text = b_db_n.read_text(errors="replace") if b_db_n.exists() else ""
        c_text = c_db_n.read_text(errors="replace") if c_db_n.exists() else ""

        if "udb-test-mutator" in b_text:
            raise AssertionError(f"Node B did not remove record on DEL:\n{b_text}")
        if "udb-test-mutator" in c_text:
            raise AssertionError(f"Node C did not remove record on multi-hop DEL:\n{c_text}")

        print("PASS: DEL mutation propagated multi-hop A -> B -> C and persisted to disk across all nodes")

    finally:
        for p in processes:
            stop(p)
        if not keep and sys.exc_info()[0] is None:
            shutil.rmtree(tmpdir, ignore_errors=True)
        elif sys.exc_info()[0] is not None:
            for lbl, pth in logs.items():
                if pth.exists():
                    print(f"--- Node {lbl} Log ---\n{pth.read_text(errors='replace')}", file=sys.stderr)
            if not keep:
                shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB multi-hop mutations integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
