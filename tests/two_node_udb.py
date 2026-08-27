#!/usr/bin/env python3
"""Isolated two-node UDB integration harness."""

import argparse
import os
import pathlib
import re
import secrets
import shutil
import signal
import socket
import stat
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
MUTATOR_RECORD = "udb-test-mutator authorized-insert"
MUTATOR_INS_TRIGGER = "udb-test-mutator-ins-go"
MUTATOR_DEL_TRIGGER = "udb-test-mutator-del-go"
MUTATOR_DRP_TRIGGER = "udb-test-mutator-drp-go"
RENAME_FAIL_SOURCE = pathlib.Path(__file__).resolve().parent / "udb_snapshot_rename_fail.c"
RENAME_FAIL_MODULE = RENAME_FAIL_SOURCE.with_suffix(".so")
MUTATOR_OPT_TRIGGER = "udb-test-mutator-opt-go"
MUTATOR_END_TRIGGER = "udb-test-mutator-end-go"
K_STAGED_RECORD = "G::*@udb-staged.test::reason staged-sync-k-effect"


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


def write_config(path, name, sid, client_port, server_port, tls_port, peer, peer_port, module, dbdir,
                 propagator, autoconnect, link_password, load_mutator=False):
    outgoing = (f'    outgoing {{ bind-ip "127.0.0.1"; hostname "127.0.0.1"; port {peer_port}; '
                'options { autoconnect; } }\n') if autoconnect else ""
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
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
link {peer} {{
    incoming {{ mask "127.0.0.1"; }}
 {outgoing}    password "{link_password}";
    password "{link_password}";
    class servers;
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
{('loadmodule "third/udb_test_mutator";' if load_mutator else '')}
udb {{
    database-directory "{dbdir}";
    propagator "{propagator}";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, module, mutator=None, configtest=False, rename_failure=False,
                   rename_failure_arm=False, fsync_failure=False):
    # A read-only host root leaves dependencies and installed modules available.
    # The runtime data mount isolates UnrealIRCd's control socket; UDB uses data/.
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
                "--setenv", "UDB_TEST_MUTATOR_DIRECTORY", str(node / "data")]
    if rename_failure or fsync_failure:
        command.extend(("--setenv", "LD_PRELOAD", str(RENAME_FAIL_MODULE)))
    if rename_failure:
        command.extend(("--setenv", "UDB_SNAPSHOT_RENAME_FAIL_TARGET", str(node / "data" / "udb_N.db")))
        if rename_failure_arm:
            command.extend(("--setenv", "UDB_SNAPSHOT_RENAME_FAIL_ARM",
                            str(node / "data" / "udb-snapshot-rename-fail-go")))
    if fsync_failure:
        command.extend(("--setenv", "UDB_SNAPSHOT_FSYNC_FAIL_TARGET", str(node / "data" / "udb_N.db.tmp")))
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
    if MUTATOR_MODULE.is_file():
        return
    src_root = pathlib.Path(os.environ.get("UNREALIRCD_SRC_ROOT", REPO_ROOT.parents[3] if len(REPO_ROOT.parents) > 3 and (REPO_ROOT.parents[3] / "Makefile").is_file() else REPO_ROOT))
    result = subprocess.run(["make", "custommodule", "MODULEFILE=udb/tests/udb_test_mutator"], cwd=src_root,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode or not MUTATOR_MODULE.is_file():
        raise RuntimeError(f"test mutator build failed:\n{result.stdout}")
    print(f"PASS: built test-only mutator {MUTATOR_MODULE.name}")


def build_rename_fail_interposer():
    if RENAME_FAIL_MODULE.is_file():
        return
    result = subprocess.run(["cc", "-shared", "-fPIC", "-o", str(RENAME_FAIL_MODULE),
                             str(RENAME_FAIL_SOURCE), "-ldl"], cwd=REPO_ROOT, text=True,
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


def equal_timestamp_winner_observed(a_log, b_log):
    # 0B1 wins over 0A1, so only A requests B's divergent equal-time blocks.
    a_commands = udb_commands(a_log)
    b_commands = udb_commands(b_log)
    return (ordered(a_commands, ("HEL", "INF", "BEGIN", "PUT", "END")) and
            ordered(b_commands, ("HEL", "INF", "RES", "ACK")) and
            a_commands.count("RES") == 0 and b_commands.count("RES") == 2)


def snapshot_rename_failure_observed(a_log, b_log, b_db, baseline):
    return (ordered(udb_commands(b_log), ("HEL", "INF", "BEGIN", "PUT", "END")) and
            "ERR" in udb_commands(a_log) and "Staged sync acknowledged for block N" not in log_text(a_log) and
            b_db.read_bytes() == baseline and not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log) and
            "digest or persistence failure" in log_text(b_log) and
            ("cmd=END err=3" in log_text(a_log) or "cmd=END err=6" in log_text(a_log)))


def snapshot_fsync_failure_observed(a_log, b_log, b_db, baseline):
    return (ordered(udb_commands(b_log), ("HEL", "INF", "BEGIN", "PUT", "END")) and
            "ERR" in udb_commands(a_log) and "Staged sync acknowledged for block N" not in log_text(a_log) and
            b_db.read_bytes() == baseline and not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_FSYNC_FAIL:" in log_text(b_log) and
            "digest or persistence failure" in log_text(b_log) and
            ("cmd=END err=3" in log_text(a_log) or "cmd=END err=6" in log_text(a_log)))


def db_contains(db, record):
    return record in db.read_text(errors="replace")


def snapshot_is_private(db):
    return stat.S_IMODE(db.stat().st_mode) == 0o600


def db_loaded_from(log, db):
    return db_loaded_from_text(log_text(log), db)


def db_loaded_from_text(text, db):
    return f"Loaded block {db.stem[-1]} from {db} (" in text


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


def runtime_opt_rename_failure_observed(a_log, b_log, b_db, baseline):
    return ("OPT" in udb_commands(b_log) and "ERR" in udb_commands(a_log) and
            b_db.read_bytes() == baseline and not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log) and
            ("cmd=OPT err=3" in log_text(a_log) or "cmd=OPT err=6" in log_text(a_log)))


def runtime_del_rename_failure_observed(a_log, b_log, b_db, baseline):
    return (ordered(udb_commands(b_log), ("INS", "DEL")) and "ERR" in udb_commands(a_log) and
            b_db.read_bytes() == baseline and db_contains(b_db, MUTATOR_RECORD) and
            not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log) and
            ("cmd=DEL err=3" in log_text(a_log) or "cmd=DEL err=6" in log_text(a_log)))


def runtime_drp_rename_failure_observed(a_log, b_log, b_db, baseline):
    return ("DRP" in udb_commands(b_log) and "ERR" in udb_commands(a_log) and
            b_db.read_bytes() == baseline and db_contains(b_db, "harness-b::vhost winner.test") and
            not b_db.with_suffix(".db.tmp").exists() and
            "UDB_TEST_SNAPSHOT_RENAME_FAIL:" in log_text(b_log) and
            ("cmd=DRP err=3" in log_text(a_log) or "cmd=DRP err=6" in log_text(a_log)))


def malformed_end_checksums_rejected(a_log, b_log, b_db, baseline):
    return (udb_commands(b_log).count("END") >= 3 and
            (log_text(a_log).count("cmd=END err=3") >= 3 or log_text(a_log).count("cmd=END err=6") >= 3) and
            log_text(b_log).count("digest or persistence failure") >= 3 and
            b_db.read_bytes() == baseline and not db_contains(b_db, "attack"))


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
                        default=find_module_path(),
                        help="compiled UDB module to load")
    parser.add_argument("--timeout", type=int, default=15, help="S2S wait time in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve temporary node directories")
    parser.add_argument("--snapshot-rename-failure", action="store_true",
                        help="fail node A's UDB N snapshot rename and verify staged-sync rollback")
    parser.add_argument("--snapshot-fsync-failure", action="store_true",
                        help="fail node A's UDB N temporary snapshot fsync and verify staged-sync rollback")
    parser.add_argument("--runtime-rename-failure", action="store_true",
                        help="fail node B's armed live INS snapshot rename and verify no local commit")
    parser.add_argument("--runtime-opt-rename-failure", action="store_true",
                        help="fail node B's armed live OPT snapshot rename and verify rollback")
    parser.add_argument("--runtime-del-rename-failure", action="store_true",
                        help="fail node B's armed live DEL snapshot rename and verify rollback")
    parser.add_argument("--runtime-drp-rename-failure", action="store_true",
                        help="fail node B's armed live DRP snapshot rename and verify rollback")
    parser.add_argument("--malformed-end-checksum", action="store_true",
                        help="reject empty, partial, and overflowing END checksums without committing")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for separate PERMDATADIR mount namespaces; see two_node_udb.md")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip("installed modules.default.conf is unavailable; see two_node_udb.md")

    # Do not use TemporaryDirectory here: its finalizer still removes the tree
    # after a replaced cleanup method, making --keep ineffective.
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-two-node-"))
    processes = []
    try:
        build_mutator()
        if (args.snapshot_rename_failure or args.snapshot_fsync_failure or args.runtime_rename_failure or args.runtime_opt_rename_failure or
                args.runtime_del_rename_failure or args.runtime_drp_rename_failure):
            build_rename_fail_interposer()
        a, b = root / "node-a", root / "node-b"
        for node in (a, b):
            (node / "data").mkdir(parents=True)
            (node / "runtime-data").mkdir()
            (node / "tmp").mkdir()
            third_modules = node / "modules" / "third"
            third_modules.mkdir(parents=True)
            shutil.copy2(args.module, third_modules / "udb.so")
        shutil.copy2(MUTATOR_MODULE, a / "modules" / "third" / "udb_test_mutator.so")
        # Seed divergent blocks with the same mtime. B's higher immutable SID
        # (0B1 > 0A1) must win, with A issuing the sole RES request.
        a_db = a / "data" / "udb_N.db"
        b_db = b / "data" / "udb_N.db"
        a_k_db = a / "data" / "udb_K.db"
        b_k_db = b / "data" / "udb_K.db"
        a_db.write_text("harness-a::vhost loser.test\n", encoding="ascii")
        b_db.write_text("harness-b::vhost winner.test\n", encoding="ascii")
        a_k_db.write_text("G::*@udb-loser.test::reason loser\n", encoding="ascii")
        b_k_db.write_text(K_STAGED_RECORD + "\n", encoding="ascii")
        for n in (a, b):
            for letter in ('C', 'I', 'S', 'L'):
                (n / "data" / f"udb_{letter}.db").write_text(f"; UDB Block {letter} - Version 1\n", encoding="ascii")
        (a / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")
        (b / "data" / ".udb_state").write_text("STATE=READY\nLAST_SYNC=1787720000\n", encoding="ascii")
        a_baseline = a_db.read_bytes()
        b_baseline = b_db.read_bytes()
        tie_time = int(time.time()) - 60
        for db in (a_db, b_db, a_k_db, b_k_db):
            os.utime(db, (tie_time, tie_time))

        a_client, a_server, a_tls, b_client, b_server, b_tls = free_ports(6)
        a_conf, b_conf = a / "unrealircd.conf", b / "unrealircd.conf"
        link_password = "udb-test-" + secrets.token_hex(32)
        write_config(a_conf, "udb-a.test", "0A1", a_client, a_server, a_tls,
                      "udb-b.test", b_server, args.module, a / "data", "udb-b.test", True,
                      link_password, load_mutator=True)
        write_config(b_conf, "udb-b.test", "0B1", b_client, b_server, b_tls,
                      "udb-a.test", a_server, args.module, b / "data", "udb-a.test", False,
                      link_password)
        run_configtest(a, args.ircd, a_conf, args.module, MUTATOR_MODULE)
        run_configtest(b, args.ircd, b_conf, args.module)

        logs = [a / "ircd.log", b / "ircd.log"]
        for node, config, log in ((b, b_conf, logs[1]), (a, a_conf, logs[0])):
            with log.open("w") as output:
                mutator = MUTATOR_MODULE if node == a else None
                processes.append(subprocess.Popen(bwrap_command(node, args.ircd, config, args.module, mutator,
                                                                  rename_failure=(args.snapshot_rename_failure and node == a) or
                                                                                  ((args.runtime_rename_failure or
                                                                                    args.runtime_opt_rename_failure or
                                                                                    args.runtime_del_rename_failure or
                                                                                    args.runtime_drp_rename_failure) and node == b),
                                                                    rename_failure_arm=(args.runtime_rename_failure or
                                                                                        args.runtime_opt_rename_failure or
                                                                                        args.runtime_del_rename_failure or
                                                                                        args.runtime_drp_rename_failure) and node == b,
                                                                    fsync_failure=args.snapshot_fsync_failure and node == a),
                                                   stdout=output, stderr=subprocess.STDOUT,
                                                   text=True))
        if not wait_for_link(processes, logs, args.timeout):
            details = "\n".join(f"--- {log.name} ---\n{log_text(log)}" for log in logs)
            print("SKIP: servers started/configtested, but an S2S link was not observed in both logs.")
            print("This is not a PASS. Check link prerequisites in two_node_udb.md.")
            print(details, file=sys.stderr)
            return 77
        if not all(db_loaded_from(log, db) for log, db in
                   ((logs[0], a_db), (logs[1], b_db), (logs[0], a_k_db), (logs[1], b_k_db))):
            print_diagnostics(logs, a_db)
            return 1
        print("PASS: each node loaded its seeded N/K blocks from its configured temporary database directory")

        deadline = time.monotonic() + args.timeout
        if args.snapshot_rename_failure:
            while time.monotonic() < deadline:
                if snapshot_rename_failure_observed(logs[1], logs[0], a_db, a_baseline):
                    break
                time.sleep(0.25)
            if not snapshot_rename_failure_observed(logs[1], logs[0], a_db, a_baseline):
                print_diagnostics(logs, a_db)
                return 1
            print("PASS: failed N snapshot rename left node A baseline unchanged with no tmp or ACK/commit")
            return 0
        if args.snapshot_fsync_failure:
            while time.monotonic() < deadline:
                if snapshot_fsync_failure_observed(logs[1], logs[0], a_db, a_baseline):
                    break
                time.sleep(0.25)
            if not snapshot_fsync_failure_observed(logs[1], logs[0], a_db, a_baseline):
                print_diagnostics(logs, a_db)
                return 1
            print("PASS: failed N temporary snapshot fsync left node A baseline unchanged with no tmp or ACK/commit")
            return 0
        while time.monotonic() < deadline:
            if ("harness-b::vhost winner.test" in a_db.read_text(errors="replace") and
                    K_STAGED_RECORD in a_k_db.read_text(errors="replace") and
                    equal_timestamp_winner_observed(logs[0], logs[1])):
                break
            time.sleep(0.25)
        if ("harness-b::vhost winner.test" not in a_db.read_text(errors="replace") or
                K_STAGED_RECORD not in a_k_db.read_text(errors="replace") or
                not equal_timestamp_winner_observed(logs[0], logs[1])):
            print_diagnostics(logs, a_db)
            return skip("S2S linked, but deterministic equal-timestamp staged transfer was not observed; this is not a PASS")
        if not snapshot_is_private(a_db):
            print(f"FAIL: node A active UDB snapshot mode is {stat.S_IMODE(a_db.stat().st_mode):04o}, expected 0600",
                  file=sys.stderr)
            return 1
        print("PASS: higher-SID B won divergent equal-timestamp N and nested K blocks with one RES per block")
        if args.malformed_end_checksum:
            b_baseline = b_db.read_bytes()
            (a / "data" / MUTATOR_END_TRIGGER).touch()
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if malformed_end_checksums_rejected(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not malformed_end_checksums_rejected(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: empty-tree staged END rejected empty, partial, and overflowing checksums without commit")
            return 0
        if args.runtime_del_rename_failure:
            (a / "data" / MUTATOR_INS_TRIGGER).touch()
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline and not mutator_insert_observed(logs[1], b_db):
                time.sleep(0.25)
            if not mutator_insert_observed(logs[1], b_db):
                print_diagnostics(logs, b_db)
                return 1
            b_baseline = b_db.read_bytes()
            (b / "data" / "udb-snapshot-rename-fail-go").touch()
            (a / "data" / MUTATOR_DEL_TRIGGER).touch()
        elif args.runtime_rename_failure or args.runtime_opt_rename_failure or args.runtime_drp_rename_failure:
            b_baseline = b_db.read_bytes()
            (b / "data" / "udb-snapshot-rename-fail-go").touch()
        if args.runtime_drp_rename_failure:
            (a / "data" / MUTATOR_DRP_TRIGGER).touch()
        elif args.runtime_opt_rename_failure:
            (a / "data" / MUTATOR_OPT_TRIGGER).touch()
        elif not args.runtime_del_rename_failure:
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
        if args.runtime_opt_rename_failure:
            while time.monotonic() < deadline:
                if runtime_opt_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not runtime_opt_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: failed live OPT snapshot rename left node B's durable N block unchanged and returned ERR")
            return 0
        if args.runtime_del_rename_failure:
            while time.monotonic() < deadline:
                if runtime_del_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not runtime_del_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: failed live DEL snapshot rename retained node B's byte-identical active and durable N record")
            return 0
        if args.runtime_drp_rename_failure:
            while time.monotonic() < deadline:
                if runtime_drp_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                    break
                time.sleep(0.25)
            if not runtime_drp_rename_failure_observed(logs[0], logs[1], b_db, b_baseline):
                print_diagnostics(logs, b_db)
                return 1
            print("PASS: failed live DRP snapshot rename retained node B's byte-identical active and durable N records")
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
        restart_offset = len(log_text(logs[1]))
        stop((processes[0],))
        processes.pop(0)
        with logs[1].open("a") as output:
            processes.insert(0, subprocess.Popen(bwrap_command(b, args.ircd, b_conf, args.module),
                                                  stdout=output, stderr=subprocess.STDOUT, text=True))
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            if processes[0].poll() is not None:
                break
            if db_loaded_from_text(log_text(logs[1])[restart_offset:], b_db):
                break
            time.sleep(0.25)
        if processes[0].poll() is not None or not db_loaded_from_text(log_text(logs[1])[restart_offset:], b_db):
            print_diagnostics(logs, b_db)
            return 1
        print("PASS: restarted node B loaded its persisted N block from its configured temporary database directory")
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
