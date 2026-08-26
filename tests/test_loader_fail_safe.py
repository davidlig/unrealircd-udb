#!/usr/bin/env python3
"""Integration tests for UDB transactional loader and fail-safe behavior."""

import argparse
import os
import pathlib
import shutil
import signal
import socket
import stat
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


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";

me {{
    name "{name}";
    info "UDB fail-safe loader integration harness";
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
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
}}
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


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-loader-test-"))
    module_path = ROOT / "src/modules/third/udb/src/udb.so"

    try:
        # -------------------------------------------------------------
        # Test 1: ENOENT (empty directory -> starts cleanly)
        # -------------------------------------------------------------
        node = tmpdir / "node1"
        data_dir = node / "data"
        data_dir.mkdir(parents=True)
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(module_path, third_modules / "udb.so")

        client_port, tls_port = free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-node1.test", "0A1", client_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start with empty DB directory:\n{stdout}")

        stop(proc)
        print("PASS: ENOENT en directorio de base de datos arrancó limpiamente")

        # -------------------------------------------------------------
        # Test 2: EACCES on one block file aborts module load
        # -------------------------------------------------------------
        node2 = tmpdir / "node2"
        data_dir2 = node2 / "data"
        data_dir2.mkdir(parents=True)
        (node2 / "runtime-data").mkdir()
        (node2 / "tmp").mkdir()
        third_modules2 = node2 / "modules" / "third"
        third_modules2.mkdir(parents=True)
        shutil.copy2(module_path, third_modules2 / "udb.so")

        unreadable_db = data_dir2 / "udb_N.db"
        unreadable_db.write_text("alice::vhost secret.test\n", encoding="ascii")
        original_bytes = unreadable_db.read_bytes()
        unreadable_db.chmod(0o000)

        client_port2, tls_port2 = free_port(), free_port()
        config2 = node2 / "unrealircd.conf"
        write_config(config2, "udb-node2.test", "0A2", client_port2, tls_port2, module_path, data_dir2)

        proc2 = subprocess.Popen(bwrap_command(node2, ircd_bin, config2),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        stop(proc2)
        stdout2, _ = proc2.communicate()

        if "Cannot open database file" not in stdout2 and "Failed to initialize database engine" not in stdout2:
            raise AssertionError(f"Expected failure log not found in stdout:\n{stdout2}")

        unreadable_db.chmod(0o600)
        if unreadable_db.read_bytes() != original_bytes:
            raise AssertionError("Original database file was modified after failed load!")

        print("PASS: EACCES en archivo de bloque abortó la carga de UDB y protegió el archivo original contra sobreescritura")

    finally:
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB loader fail-safe integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
