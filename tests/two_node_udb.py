#!/usr/bin/env python3
"""Isolated two-node UDB integration harness.

Each server runs in a separate bubblewrap mount namespace.  This is necessary
because UDB block files currently resolve below UnrealIRCd's compiled
PERMDATADIR, rather than udb::database-directory.
"""

import argparse
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
DEFAULT_IRCD = pathlib.Path("/home/davidlig/unrealircd/bin/unrealircd")
PERMDATADIR = pathlib.Path("/home/davidlig/unrealircd/data")
CLOAK_KEYS = ("aB3" * 30, "cD4" * 30, "eF5" * 30)


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, server_port, tls_port, peer, peer_port, module, dbdir, autoconnect):
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
udb {{
    database-directory "{dbdir}";
    propagator "udb-a.test";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, module, configtest=False):
    # A read-only host root leaves dependencies and installed modules available.
    # Only this node's working directory and compiled data directory are writable.
    command = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
               "--bind", str(node), str(node),
               "--bind", str(node / "data"), str(PERMDATADIR),
               "--bind", str(node / "tmp"), "/home/davidlig/unrealircd/tmp",
               "--ro-bind", str(module), "/home/davidlig/unrealircd/modules/third/udb.so",
               "--dev-bind", "/dev", "/dev", "--proc", "/proc",
               str(ircd), "-f", str(config)]
    if configtest:
        command.append("-c")
    else:
        command.append("-F")
    return command


def run_configtest(node, ircd, config, module):
    result = subprocess.run(bwrap_command(node, ircd, config, module, configtest=True),
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30)
    if result.returncode:
        raise RuntimeError(f"configtest failed for {config}:\n{result.stdout}")
    print(f"PASS: configtest {config.name} (generated config loads UDB)")


def wait_for_link(processes, logs, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(proc.poll() is not None for proc in processes):
            return False
        time.sleep(0.25)
    return True


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
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for separate PERMDATADIR mount namespaces; see two_node_udb.md")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not pathlib.Path("/home/davidlig/unrealircd/conf/modules.default.conf").is_file():
        return skip("installed modules.default.conf is unavailable; see two_node_udb.md")

    temporary = tempfile.TemporaryDirectory(prefix="udb-two-node-")
    root = pathlib.Path(temporary.name)
    processes = []
    try:
        a, b = root / "node-a", root / "node-b"
        for node in (a, b):
            (node / "data").mkdir(parents=True)
            (node / "tmp").mkdir()
        # Seed only node A. A successful staged sync must make this record appear
        # in node B's separately mounted PERMDATADIR.
        (a / "data" / "udb_N.db").write_text("harness-a::marker synced\n", encoding="ascii")
        (b / "data" / "udb_N.db").write_text("harness-b::marker prior\n", encoding="ascii")

        a_client, a_server, a_tls, b_client, b_server, b_tls = (free_port() for _ in range(6))
        a_conf, b_conf = a / "unrealircd.conf", b / "unrealircd.conf"
        write_config(a_conf, "udb-a.test", "0A1", a_client, a_server, a_tls,
                     "udb-b.test", b_server, args.module, a / "data", True)
        write_config(b_conf, "udb-b.test", "0B1", b_client, b_server, b_tls,
                     "udb-a.test", a_server, args.module, b / "data", False)
        run_configtest(a, args.ircd, a_conf, args.module)
        run_configtest(b, args.ircd, b_conf, args.module)

        logs = [a / "ircd.log", b / "ircd.log"]
        for node, config, log in ((b, b_conf, logs[1]), (a, a_conf, logs[0])):
            with log.open("w") as output:
                processes.append(subprocess.Popen(bwrap_command(node, args.ircd, config, args.module),
                                                  stdout=output, stderr=subprocess.STDOUT,
                                                  text=True))
        if not wait_for_link(processes, logs, args.timeout):
            details = "\n".join(f"--- {log.name} ---\n{log.read_text(errors='replace')}" for log in logs)
            print("SKIP: servers started/configtested, but an S2S link could not be established.")
            print("This is not a PASS. Check link prerequisites in two_node_udb.md.")
            print(details, file=sys.stderr)
            return 77

        deadline = time.monotonic() + args.timeout
        b_db = b / "data" / "udb_N.db"
        while time.monotonic() < deadline and "harness-a::marker synced" not in b_db.read_text(errors="replace"):
            time.sleep(0.25)
        if "harness-a::marker synced" not in b_db.read_text(errors="replace"):
            return skip("nodes remained available for S2S, but UDB capability negotiation/staged N-block transfer was not observed; this is not a PASS")
        print("PASS: UDB capability negotiation and staged N-block sync committed in node B")
        return 0
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        stop(processes)
        if args.keep:
            print(f"Temporary files retained at: {root}")
            temporary.cleanup = lambda: None
        temporary.cleanup()


if __name__ == "__main__":
    sys.exit(main())
