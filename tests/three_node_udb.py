#!/usr/bin/env python3
"""Isolated three-node A-B-C UDB staged-sync integration harness."""

import argparse
import os
import pathlib
import re
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
N_MARKER = "harness-a::vhost propagated.test\n"
MUTATOR_SOURCE = pathlib.Path(__file__).resolve().parent / "udb_test_mutator.c"
MUTATOR_MODULE = MUTATOR_SOURCE.with_suffix(".so")
MUTATOR_RECORDS = {
    "A-B": "udb-test-mutator authorized-insert",
    "B-C": "udb-test-mutator authorized-insert-b-c",
}
STAGED_AUTH_TRIGGER = "udb-test-mutator-staged-authorization-go"


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    for candidate in (
        REPO_ROOT / "src" / "udb.so",
        REPO_ROOT / "dist" / "udb.so",
        RUNTIME_ROOT / "modules/third/udb.so",
    ):
        if candidate.is_file():
            return candidate
    return REPO_ROOT / "src" / "udb.so"


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


def write_config(path, name, sid, ports, links, module, dbdir, propagator, link_password,
                 load_mutator=False):
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
    info "UDB isolated integration node";
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
                "--setenv", "UDB_TEST_MUTATOR_DIRECTORY", str(node / "data"),
                str(ircd), "-f", str(config)]
    command.append("-c" if configtest else "-F")
    return command


def bwrap_unavailable(output):
    return any(text in output for text in ("Creating new namespace failed", "Operation not permitted",
                                            "No permissions to create a new namespace", "bwrap: "))


def run_configtest(node, ircd, config):
    result = subprocess.run(bwrap_command(node, ircd, config, configtest=True), text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    if result.returncode:
        if bwrap_unavailable(result.stdout):
            raise EnvironmentUnavailable(result.stdout.strip())
        raise RuntimeError(f"configtest failed for {config}:\n{result.stdout}")
    print(f"PASS: configtest {config.name} (generated config loads UDB)")


def build_mutator():
    if MUTATOR_MODULE.is_file():
        return
    src_root = pathlib.Path(os.environ.get("UNREALIRCD_SRC_ROOT", REPO_ROOT.parents[3] if len(REPO_ROOT.parents) > 3 and (REPO_ROOT.parents[3] / "Makefile").is_file() else REPO_ROOT))
    result = subprocess.run(["make", "custommodule", "MODULEFILE=udb/tests/udb_test_mutator"], cwd=src_root,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode or not MUTATOR_MODULE.is_file():
        raise RuntimeError(f"test mutator build failed:\n{result.stdout}")
    print(f"PASS: built test-only mutator {MUTATOR_MODULE.name}")


def log_text(log):
    try:
        return log.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def link_observed(log, count=1):
    text = log_text(log)
    return text.count("Server linked:") >= count and text.count(" is now synced") >= count


def wait_for_links(processes, checks, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(process.poll() is not None for process in processes):
            return False
        if all(link_observed(log, count) for log, count in checks):
            return True
        time.sleep(0.25)
    return False


def udb_commands(log):
    return re.findall(r"\[UDB\] S2S DB received: .* subcmd=(\w+)", log_text(log))


def udb_commands_text(text):
    return re.findall(r"\[UDB\] S2S DB received: .* subcmd=(\w+)", text)


def ordered(commands, expected):
    position = 0
    for command in commands:
        if command == expected[position]:
            position += 1
            if position == len(expected):
                return True
    return False


def staged_snapshot_observed(receiver_log):
    return ordered(udb_commands(receiver_log), ("HEL", "INF", "BEGIN", "PUT", "END"))


def wait_for_snapshot(db, receiver_log, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if N_MARKER in db.read_text(errors="replace") and staged_snapshot_observed(receiver_log):
            return True
        time.sleep(0.25)
    return False


def db_contains(db, record):
    return record in db.read_text(errors="replace")


def db_loaded_from(log, db):
    return f"Loaded block {db.stem[-1]} from {db} (" in log_text(log)


def mutator_insert_observed(receiver_log, db, record):
    return "INS" in udb_commands(receiver_log) and db_contains(db, record)


def mutator_delete_observed(receiver_log, db, record):
    return ordered(udb_commands(receiver_log), ("INS", "DEL")) and not db_contains(db, record)


def wait_for_mutation(receiver_log, db, record, deleted, timeout):
    deadline = time.monotonic() + timeout
    observed = mutator_delete_observed if deleted else mutator_insert_observed
    while time.monotonic() < deadline:
        if observed(receiver_log, db, record):
            return True
        time.sleep(0.25)
    return False


def arm_mutators(nodes):
    for node in nodes:
        (node / "data" / "udb-test-mutator-go").touch()


def staged_authorization_rejected(b_log, c_log, b_log_offset, c_log_offset, b_db, baseline):
    b_commands = udb_commands_text(log_text(b_log)[b_log_offset:])
    c_commands = udb_commands_text(log_text(c_log)[c_log_offset:])
    return (all(command in b_commands for command in ("BEGIN", "PUT", "END", "RES")) and
            c_commands.count("ERR") >= 3 and
            b_db.read_bytes() == baseline)


def print_diagnostics(stage, logs, dbs=()):
    print(f"DIAGNOSTIC: {stage} did not produce the required UDB evidence.", file=sys.stderr)
    for label, log in logs:
        commands = ", ".join(udb_commands(log)) or "(none)"
        print(f"DIAGNOSTIC: {label} received UDB commands: {commands}", file=sys.stderr)
        evidence = [line for line in log_text(log).splitlines()
                    if "Server linked:" in line or " is now synced" in line or "[UDB]" in line]
        print(f"--- {label} link/frame evidence ({log}) ---", file=sys.stderr)
        print("\n".join(evidence) or "(none)", file=sys.stderr)
    for label, db in dbs:
        print(f"--- {label} database ({db}) ---", file=sys.stderr)
        print(db.read_text(errors="replace") if db.exists() else "(missing)", file=sys.stderr)


def stop(processes):
    for process in processes:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
    for process in processes:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    parser.add_argument("--timeout", type=int, default=15, help="per-link and per-stage wait in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve temporary node directories")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for separate PERMDATADIR mount namespaces; see three_node_udb.md")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip("installed modules.default.conf is unavailable; see three_node_udb.md")

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-three-node-"))
    processes = []
    try:
        build_mutator()
        a, b, c = root / "node-a", root / "node-b", root / "node-c"
        for node in (a, b, c):
            (node / "data").mkdir(parents=True)
            (node / "runtime-data").mkdir()
            (node / "tmp").mkdir()
            (node / "modules" / "third").mkdir(parents=True)
            shutil.copy2(args.module, node / "modules" / "third" / "udb.so")
        for node in (a, b, c):
            shutil.copy2(MUTATOR_MODULE, node / "modules" / "third" / "udb_test_mutator.so")
        a_db, b_db, c_db = (node / "data" / "udb_N.db" for node in (a, b, c))
        # Only A has a record. Empty, old placeholders make B and C request A's block.
        a_db.write_text(N_MARKER, encoding="ascii")
        (a / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")
        for db in (b_db, c_db):
            db.touch()
            old_time = time.time() - 60
            os.utime(db, (old_time, old_time))
        (b / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787710000\n", encoding="ascii")
        (c / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787710000\n", encoding="ascii")

        raw_ports = free_ports(9)
        ports = [tuple(raw_ports[i * 3:(i + 1) * 3]) for i in range(3)]
        a_conf, b_conf, c_conf = (node / "unrealircd.conf" for node in (a, b, c))
        link_password = "udb-test-" + secrets.token_hex(32)
        write_config(a_conf, "udb-a.test", "0A1", ports[0], (("udb-b.test", ports[1][1], True),),
                     args.module, a / "data", "udb-b.test", link_password, True)
        write_config(b_conf, "udb-b.test", "0B1", ports[1],
                     (("udb-a.test", ports[0][1], False), ("udb-c.test", ports[2][1], True)),
                     args.module, b / "data", "udb-a.test", link_password, True)
        write_config(c_conf, "udb-c.test", "0C1", ports[2], (("udb-b.test", ports[1][1], False),),
                     args.module, c / "data", "udb-b.test", link_password, True)
        for node, config in ((a, a_conf), (b, b_conf), (c, c_conf)):
            run_configtest(node, args.ircd, config)

        logs = {label: node / "ircd.log" for label, node in (("A", a), ("B", b), ("C", c))}
        def start(label, node, config):
            with logs[label].open("w") as output:
                processes.append(subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=output,
                                                  stderr=subprocess.STDOUT, text=True))

        # C stays down until B has committed A's record, forcing B's later link
        # synchronization to be the observed B-to-C propagation path.
        start("B", b, b_conf)
        start("A", a, a_conf)
        if not wait_for_links(processes, ((logs["A"], 1), (logs["B"], 1)), args.timeout):
            print_diagnostics("A-B link timeout", (("node A", logs["A"]), ("node B", logs["B"])))
            return skip("A-B S2S link was not observed before timeout; this is not a PASS")
        if not all(db_loaded_from(logs[label], db) for label, db in (("A", a_db), ("B", b_db))):
            print_diagnostics("A-B database load", (("node A", logs["A"]), ("node B", logs["B"])),
                              (("node A", a_db), ("node B", b_db)))
            return 1
        print("PASS: A and B loaded their seeded N blocks from configured temporary database directories")
        if not wait_for_snapshot(b_db, logs["B"], args.timeout):
            print_diagnostics("A-to-B timeout", (("node A", logs["A"]), ("node B", logs["B"])))
            return skip("A-B linked, but A's staged N-block did not commit in B; this is not a PASS")
        start("C", c, c_conf)
        if not wait_for_links(processes, ((logs["B"], 2), (logs["C"], 1)), args.timeout):
            print_diagnostics("B-C link timeout", (("node B", logs["B"]), ("node C", logs["C"])))
            return skip("B-C S2S link was not observed before timeout; this is not a PASS")
        if not db_loaded_from(logs["C"], c_db):
            print_diagnostics("C database load", (("node C", logs["C"]),), (("node C", c_db),))
            return 1
        print("PASS: C loaded its seeded N block from its configured temporary database directory")
        if not wait_for_snapshot(c_db, logs["C"], args.timeout):
            print_diagnostics("B-to-C timeout", (("node B", logs["B"]), ("node C", logs["C"])))
            return skip("B-C linked, but A's record did not staged-sync from B into C; this is not a PASS")
        print("PASS: A's staged N-block committed in B before B synchronized it to C")
        b_log_offset = len(log_text(logs["B"]))
        c_log_offset = len(log_text(logs["C"]))
        b_baseline = b_db.read_bytes()
        (c / "data" / STAGED_AUTH_TRIGGER).touch()
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            if staged_authorization_rejected(logs["B"], logs["C"], b_log_offset, c_log_offset,
                                             b_db, b_baseline):
                break
            time.sleep(0.25)
        if not staged_authorization_rejected(logs["B"], logs["C"], b_log_offset, c_log_offset,
                                             b_db, b_baseline):
            print_diagnostics("non-propagator staged authorization", (("node B", logs["B"]),
                                                                        ("node C", logs["C"])),
                              (("node B", b_db),))
            return 1
        print("PASS: HEL-confirmed non-propagator C could not import a staged block from or request an export from B")
        arm_mutators((a,))
        if not wait_for_mutation(logs["B"], b_db, MUTATOR_RECORDS["A-B"], False, args.timeout):
            print_diagnostics("A-to-B authorized INS timeout", (("node A", logs["A"]), ("node B", logs["B"])),
                              (("node B", b_db),))
            return skip("A-B-C staged-sync completed, but B did not durably apply the authorized A-to-B INS")
        print("PASS: B durably applied the authorized A-to-B INS")
        if not wait_for_mutation(logs["B"], b_db, MUTATOR_RECORDS["A-B"], True, args.timeout):
            print_diagnostics("A-to-B authorized DEL timeout", (("node A", logs["A"]), ("node B", logs["B"])),
                              (("node B", b_db),))
            return skip("A-B-C staged-sync completed, but B did not durably apply the authorized A-to-B DEL")
        print("PASS: B durably applied the authorized A-to-B DEL")
        arm_mutators((b,))
        if not wait_for_mutation(logs["C"], c_db, MUTATOR_RECORDS["B-C"], False, args.timeout):
            print_diagnostics("B-to-C authorized INS timeout", (("node B", logs["B"]), ("node C", logs["C"])),
                              (("node C", c_db),))
            return skip("B-C staged-sync completed, but C did not durably apply the authorized B-to-C INS")
        print("PASS: C durably applied the authorized B-to-C INS")
        if not wait_for_mutation(logs["C"], c_db, MUTATOR_RECORDS["B-C"], True, args.timeout):
            print_diagnostics("B-to-C authorized DEL timeout", (("node B", logs["B"]), ("node C", logs["C"])),
                              (("node C", c_db),))
            return skip("B-C staged-sync completed, but C did not durably apply the authorized B-to-C DEL")
        print("PASS: C durably applied the authorized B-to-C DEL")
        return 0
    except EnvironmentUnavailable as exc:
        return skip(f"bubblewrap isolation is unavailable: {exc}")
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        stop(processes)
        if args.keep:
            print(f"Temporary files retained at: {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
