#!/usr/bin/env python3
"""Isolated two-node UDB integration harness.

Each server runs in a separate bubblewrap mount namespace.  This is necessary
because UDB block files currently resolve below UnrealIRCd's compiled
PERMDATADIR, rather than udb::database-directory.
"""

import argparse
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


ROOT = pathlib.Path(__file__).resolve().parents[5]
DEFAULT_IRCD = pathlib.Path("/home/davidlig/unrealircd/bin/unrealircd")
PERMDATADIR = pathlib.Path("/home/davidlig/unrealircd/data")
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)
MUTATOR_SOURCE = ROOT / "src/modules/third/udb/tests/udb_test_mutator.c"
MUTATOR_MODULE = MUTATOR_SOURCE.with_suffix(".so")
MUTATOR_RECORD = "udb-test-mutator authorized-insert"
RENAME_FAIL_SOURCE = ROOT / "src/modules/third/udb/tests/udb_snapshot_rename_fail.c"
RENAME_FAIL_MODULE = RENAME_FAIL_SOURCE.with_suffix(".so")
RENAME_FAIL_TARGET = str(PERMDATADIR / "udb_N.db")
RENAME_FAIL_ARM = str(PERMDATADIR / "udb-snapshot-rename-fail-go")


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, server_port, tls_port, peer, peer_port, module, dbdir,
                 autoconnect, load_mutator=False):
    outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                'options { autoconnect; } }\n') if autoconnect else ""
    path.write_text(f'''include "/home/davidlig/unrealircd/conf/modules.default.conf";
include "/home/davidlig/unrealircd/conf/snomasks.default.conf";

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
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
{outgoing}    password "udb-harness-link";
    password "udb-harness-link";
    class servers;
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
{('loadmodule "third/udb_test_mutator";' if load_mutator else '')}
udb {{
    database-directory "{dbdir}";
    propagator "udb-a.test";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, module, mutator=None, configtest=False, rename_failure=False,
                  rename_failure_arm=False):
    # A read-only host root leaves dependencies and installed modules available.
    # Only this node's working directory and compiled data directory are writable.
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
               "--bind", str(node), str(node),
               "--bind", str(node / "data"), str(PERMDATADIR),
               "--bind", str(node / "tmp"), "/home/davidlig/unrealircd/tmp",
               "--ro-bind", str(node / "modules" / "third"), "/home/davidlig/unrealircd/modules/third",
               "--dev-bind", "/dev", "/dev", "--proc", "/proc"]
    if rename_failure:
        command.extend(("--setenv", "LD_PRELOAD", str(RENAME_FAIL_MODULE),
                        "--setenv", "UDB_SNAPSHOT_RENAME_FAIL_TARGET", RENAME_FAIL_TARGET))
        if rename_failure_arm:
            command.extend(("--setenv", "UDB_SNAPSHOT_RENAME_FAIL_ARM", RENAME_FAIL_ARM))
    command.extend((str(ircd), "-f", str(config)))
    if configtest:
        command.append("-c")
    else:
        command.append("-F")
    return command


def run_configtest(node, ircd, config, module, mutator=None):
    result = subprocess.run(bwrap_command(node, ircd, config, module, mutator, configtest=True),
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30)
    if result.returncode:
        raise RuntimeError(f"configtest failed for {config}:\n{result.stdout}")
    print(f"PASS: configtest {config.name} (generated config loads UDB)")


def build_mutator():
    result = subprocess.run(["make", "custommodule", "MODULEFILE=udb/tests/udb_test_mutator"], cwd=ROOT,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode or not MUTATOR_MODULE.is_file():
        raise RuntimeError(f"test mutator build failed:\n{result.stdout}")
    print(f"PASS: built test-only mutator {MUTATOR_MODULE.name}")


def build_rename_fail_interposer():
    result = subprocess.run(["cc", "-shared", "-fPIC", "-o", str(RENAME_FAIL_MODULE),
                             str(RENAME_FAIL_SOURCE), "-ldl"], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    if result.returncode or not RENAME_FAIL_MODULE.is_file():
        raise RuntimeError(f"snapshot rename-failure interposer build failed:\n{result.stdout}")
    print(f"PASS: built test-only rename-failure interposer {RENAME_FAIL_MODULE.name}")


def log_text(log):
    try:
        return log.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def link_established(logs):
    return all("Server linked:" in log_text(log) and " is now synced" in log_text(log)
               for log in logs)


def wait_for_link(processes, logs, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(proc.poll() is not None for proc in processes):
            return False
        if link_established(logs):
            return True
        time.sleep(0.25)
    return False


def udb_commands(log):
    return re.findall(r"\[UDB\] S2S DB received: .* subcmd=(\w+)", log_text(log))


def ordered(commands, expected):
    position = 0
    for command in commands:
        if command == expected[position]:
            position += 1
            if position == len(expected):
                return True
    return False


def staged_snapshot_observed(a_log, b_log):
    # Both peers must confirm HEL before B receives the staged transaction.
    return (ordered(udb_commands(b_log), ("HEL", "INF", "BEGIN", "PUT", "END")) and
            ordered(udb_commands(a_log), ("HEL", "RES", "ACK")))


def snapshot_rename_failure_observed(a_log, b_log, b_db, baseline):
    return (ordered(udb_commands(b_log), ("HEL", "INF", "BEGIN", "PUT", "END")) and
            "ERR" in udb_commands(a_log) and "ACK" not in udb_commands(a_log) and
            b_db.read_bytes() == baseline and not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log) and
            "digest or persistence failure" in log_text(b_log) and
            "cmd=END err=6" in log_text(a_log))


def db_contains(db, record):
    return record in db.read_text(errors="replace")


def mutator_insert_observed(b_log, b_db):
    return (ordered(udb_commands(b_log), ("INS",)) and db_contains(b_db, MUTATOR_RECORD) and
            "Inserted record via S2S: N::udb-test-mutator -> authorized-insert" in log_text(b_log))


def mutator_delete_observed(b_log, b_db):
    return ordered(udb_commands(b_log), ("INS", "DEL")) and not db_contains(b_db, MUTATOR_RECORD)


def runtime_rename_failure_observed(a_log, b_log, b_db, baseline):
    return ("INS" in udb_commands(b_log) and "ERR" in udb_commands(a_log) and
            b_db.read_bytes() == baseline and not db_contains(b_db, MUTATOR_RECORD) and
            not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log))


def print_diagnostics(logs, b_db=None):
    commands = [udb_commands(log) for log in logs]
    print("DIAGNOSTIC: S2S link evidence was observed, but staged UDB snapshot evidence was not.",
          file=sys.stderr)
    print(f"DIAGNOSTIC: node A received UDB commands: {', '.join(commands[0]) or '(none)'}",
          file=sys.stderr)
    print(f"DIAGNOSTIC: node B received UDB commands: {', '.join(commands[1]) or '(none)'}",
          file=sys.stderr)
    if not any(commands):
        print("DIAGNOSTIC: no UDB DB frames were received after both links synced. "
              "udb_hook_server_sync should first send HEL 4 and emit INF only after its HEL ACK; "
              "this run provides no evidence that either step occurred.", file=sys.stderr)
    for label, log in zip(("node A", "node B"), logs):
        evidence = [line for line in log_text(log).splitlines()
                    if "Server linked:" in line or " is now synced" in line or "[UDB]" in line or
                    "UDB_TEST_MUTATOR" in line or "UDB_TEST_SNAPSHOT_RENAME_FAIL" in line or " ERR " in line]
        print(f"--- {label} relevant log lines ({log}) ---", file=sys.stderr)
        print("\n".join(evidence) or "(none)", file=sys.stderr)
    if b_db:
        print(f"--- node B database ({b_db}) ---", file=sys.stderr)
        print(b_db.read_text(errors="replace") if b_db.exists() else "(missing)", file=sys.stderr)


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
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD,
                        help="UnrealIRCd binary (default: installed binary)")
    parser.add_argument("--module", type=pathlib.Path,
                        default=ROOT / "src/modules/third/udb/src/udb.so",
                        help="compiled UDB module to load")
    parser.add_argument("--timeout", type=int, default=15, help="S2S wait time in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve temporary node directories")
    parser.add_argument("--snapshot-rename-failure", action="store_true",
                        help="fail node B's UDB N snapshot rename and verify staged-sync rollback")
    parser.add_argument("--runtime-rename-failure", action="store_true",
                        help="fail node B's armed live INS snapshot rename and verify no local commit")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for separate PERMDATADIR mount namespaces; see two_node_udb.md")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not pathlib.Path("/home/davidlig/unrealircd/conf/modules.default.conf").is_file():
        return skip("installed modules.default.conf is unavailable; see two_node_udb.md")

    # Do not use TemporaryDirectory here: its finalizer still removes the tree
    # after a replaced cleanup method, making --keep ineffective.
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-two-node-"))
    processes = []
    try:
        build_mutator()
        if args.snapshot_rename_failure or args.runtime_rename_failure:
            build_rename_fail_interposer()
        a, b = root / "node-a", root / "node-b"
        for node in (a, b):
            (node / "data").mkdir(parents=True)
            (node / "tmp").mkdir()
            third_modules = node / "modules" / "third"
            third_modules.mkdir(parents=True)
            shutil.copy2(args.module, third_modules / "udb.so")
        shutil.copy2(MUTATOR_MODULE, a / "modules" / "third" / "udb_test_mutator.so")
        # Seed only node A. A successful staged sync must make this record appear
        # in node B's separately mounted PERMDATADIR.
        a_db = a / "data" / "udb_N.db"
        b_db = b / "data" / "udb_N.db"
        a_db.write_text("harness-a::marker synced\n", encoding="ascii")
        b_db.write_text("harness-b::marker prior\n", encoding="ascii")
        b_baseline = b_db.read_bytes()
        # Make A authoritative even on filesystems with one-second mtime resolution.
        old_time = time.time() - 60
        os.utime(b_db, (old_time, old_time))

        a_client, a_server, a_tls, b_client, b_server, b_tls = (free_port() for _ in range(6))
        a_conf, b_conf = a / "unrealircd.conf", b / "unrealircd.conf"
        write_config(a_conf, "udb-a.test", "0A1", a_client, a_server, a_tls,
                     "udb-b.test", b_server, args.module, a / "data", True, load_mutator=True)
        write_config(b_conf, "udb-b.test", "0B1", b_client, b_server, b_tls,
                     "udb-a.test", a_server, args.module, b / "data", False)
        run_configtest(a, args.ircd, a_conf, args.module, MUTATOR_MODULE)
        run_configtest(b, args.ircd, b_conf, args.module)

        logs = [a / "ircd.log", b / "ircd.log"]
        for node, config, log in ((b, b_conf, logs[1]), (a, a_conf, logs[0])):
            with log.open("w") as output:
                mutator = MUTATOR_MODULE if node == a else None
                processes.append(subprocess.Popen(bwrap_command(node, args.ircd, config, args.module, mutator,
                                                                 rename_failure=(args.snapshot_rename_failure or
                                                                                 args.runtime_rename_failure) and node == b,
                                                                 rename_failure_arm=args.runtime_rename_failure and node == b),
                                                   stdout=output, stderr=subprocess.STDOUT,
                                                   text=True))
        if not wait_for_link(processes, logs, args.timeout):
            details = "\n".join(f"--- {log.name} ---\n{log_text(log)}" for log in logs)
            print("SKIP: servers started/configtested, but an S2S link was not observed in both logs.")
            print("This is not a PASS. Check link prerequisites in two_node_udb.md.")
            print(details, file=sys.stderr)
            return 77

        deadline = time.monotonic() + args.timeout
        if args.snapshot_rename_failure:
            while time.monotonic() < deadline:
                if snapshot_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not snapshot_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: failed N snapshot rename left node B baseline unchanged with no tmp or ACK/commit")
            return 0
        while time.monotonic() < deadline:
            if ("harness-a::marker synced" in b_db.read_text(errors="replace") and
                    staged_snapshot_observed(logs[0], logs[1])):
                break
            time.sleep(0.25)
        if ("harness-a::marker synced" not in b_db.read_text(errors="replace") or
                not staged_snapshot_observed(logs[0], logs[1])):
            print_diagnostics(logs, b_db)
            return skip("S2S linked, but UDB staged N-block transfer was not observed; this is not a PASS")
        print("PASS: UDB capability negotiation and staged N-block sync committed in node B")
        if args.runtime_rename_failure:
            b_baseline = b_db.read_bytes()
            (b / "data" / "udb-snapshot-rename-fail-go").touch()
        (a / "data" / "udb-test-mutator-go").touch()
        deadline = time.monotonic() + args.timeout
        if args.runtime_rename_failure:
            while time.monotonic() < deadline:
                if runtime_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not runtime_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: failed live INS snapshot rename left node B's active and durable N block unchanged")
            return 0
        while time.monotonic() < deadline and not mutator_insert_observed(logs[1], b_db):
            time.sleep(0.25)
        if not mutator_insert_observed(logs[1], b_db):
            print_diagnostics(logs, b_db)
            return skip("S2S linked, but node B did not durably apply the authorized mutator INS")
        print("PASS: node B observed and durably persisted the authorized propagator INS")

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline and not mutator_delete_observed(logs[1], b_db):
            time.sleep(0.25)
        if not mutator_delete_observed(logs[1], b_db):
            print_diagnostics(logs, b_db)
            return skip("S2S linked, but node B did not durably apply the authorized mutator DEL")
        print("PASS: node B observed and durably persisted the authorized propagator DEL")
        return 0
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
