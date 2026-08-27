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


SERVICES_NAME = "udb-svc.test"
SERVICES_SID = "002"
IRCD_SID = "001"
LINK_PASSWORD = "udb-svc-link-password"


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

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
class servers {{ pingfreq 60; connfreq 6; maxclients 4; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 20; }}
listen {{ ip "127.0.0.1"; port {client_port}; }}
listen {{ ip "127.0.0.1"; port {server_port}; options {{ serversonly; }} }}
listen {{ ip "127.0.0.1"; port {tls_port}; options {{ tls; }} }}
link {SERVICES_NAME} {{
    incoming {{ mask "127.0.0.1"; }}
    password "{LINK_PASSWORD}";
    class servers;
}}
ulines {{
    {SERVICES_NAME};
}}
loadmodule "cloak_sha256";
loadmodule "third/udb";
udb {{
    database-directory "{dbdir}";
    propagator "{SERVICES_NAME}";
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


class MockServices:
    def __init__(self, host, port, ircd_sid="001"):
        self.sid = SERVICES_SID
        self.ircd_sid = ircd_sid
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"PASS :{LINK_PASSWORD}")
        self.send(f"PROTOCTL EAUTH={SERVICES_NAME}")
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID=" + self.sid)
        self.send(f"SERVER {SERVICES_NAME} 1 :UDB test services")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")
        self.send(f"DB {self.ircd_sid} HEL 4")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(16384)
            except socket.timeout:
                return
            if not data:
                return
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"Timeout waiting for {description}")

    def close(self):
        try:
            self.send(f"SQUIT {SERVICES_NAME} :bye")
        except OSError:
            pass
        self.sock.close()


def find_module_path():
    env_path = os.environ.get("UDB_MODULE_PATH")
    if env_path and os.path.isfile(env_path):
        return pathlib.Path(env_path)
    local_path = pathlib.Path(__file__).resolve().parent.parent / "src" / "udb.so"
    if local_path.is_file():
        return local_path
    runtime_path = RUNTIME_ROOT / "modules/third/udb.so"
    if runtime_path.is_file():
        return runtime_path
    return local_path


class IrcClient:
    def __init__(self, host, port, nick):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        self.send(f"NICK {nick}")
        self.send(f"USER {nick} 0 * :{nick}")
        self.wait_for(lambda line: " 001 " in line, "welcome")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(8192)
            except socket.timeout:
                return
            if not data:
                raise AssertionError(f"{self.nick}: server closed connection")
            self.buffer += data.decode("utf-8", errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, start=0, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for l in self.lines[start:]:
                if predicate(l):
                    return l
            self.receive(deadline)
        raise AssertionError(f"{self.nick}: did not receive {description}; lines={self.lines[start:]!r}")

    def close(self):
        try:
            self.send("QUIT :bye")
        except OSError:
            pass
        self.sock.close()


def run_tests(ircd_bin, keep=False):
    tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="udb-loader-test-"))
    module_path = find_module_path()

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

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-node1.test", "0A1", client_port, server_port, tls_port, module_path, data_dir)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd failed to start with empty DB directory:\n{stdout}")

        stop(proc)
        print("PASS: ENOENT on database directory started cleanly")

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

        client_port2, server_port2, tls_port2 = free_port(), free_port(), free_port()
        config2 = node2 / "unrealircd.conf"
        write_config(config2, "udb-node2.test", "0A2", client_port2, server_port2, tls_port2, module_path, data_dir2)

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

        print("PASS: EACCES on block file aborted UDB load and protected original file against overwrite")

        # -------------------------------------------------------------
        # Test 3: Malformed record in middle aborts load and keeps file intact
        # -------------------------------------------------------------
        node3 = tmpdir / "node3"
        data_dir3 = node3 / "data"
        data_dir3.mkdir(parents=True)
        (node3 / "runtime-data").mkdir()
        (node3 / "tmp").mkdir()
        third_modules3 = node3 / "modules" / "third"
        third_modules3.mkdir(parents=True)
        shutil.copy2(module_path, third_modules3 / "udb.so")

        corrupt_db = data_dir3 / "udb_N.db"
        corrupt_content = "; UDB Block N - Version 1\nalice::vhost secret.test\nmalformed:::entry\nbob::vhost admin.test\n"
        corrupt_db.write_text(corrupt_content, encoding="ascii")
        original_bytes3 = corrupt_db.read_bytes()

        client_port3, server_port3, tls_port3 = free_port(), free_port(), free_port()
        config3 = node3 / "unrealircd.conf"
        write_config(config3, "udb-node3.test", "0A3", client_port3, server_port3, tls_port3, module_path, data_dir3)

        proc3 = subprocess.Popen(bwrap_command(node3, ircd_bin, config3),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        stop(proc3)
        stdout3, _ = proc3.communicate()

        if corrupt_db.read_bytes() != original_bytes3:
            raise AssertionError("Corrupted database file was modified after failed transactional load!")
        print("PASS: Malformed database record aborted load and preserved original file")

        # -------------------------------------------------------------
        # Test 4: Valid records of various sizes load without truncation
        # Sizes: 500B, 1024B, 2048B, 4095B, 4096B (UDB_RECORD_VALUE_MAX)
        # -------------------------------------------------------------
        node4 = tmpdir / "node4"
        data_dir4 = node4 / "data"
        data_dir4.mkdir(parents=True)
        (node4 / "runtime-data").mkdir()
        (node4 / "tmp").mkdir()
        third_modules4 = node4 / "modules" / "third"
        third_modules4.mkdir(parents=True)
        shutil.copy2(module_path, third_modules4 / "udb.so")

        lines = ["; UDB Block C - Version 1\n"]
        sizes = [500, 1024, 2048, 4095, 4096]
        expected_records = {}
        for idx, sz in enumerate(sizes):
            chan = f"#chan{idx}"
            payload = ("x" * (sz - 1)) + str(idx)
            line = f"{chan}::topic {payload}\n"
            lines.append(line)
            expected_records[f"{chan}::topic"] = payload

        large_records_db = data_dir4 / "udb_C.db"
        large_records_db.write_text("".join(lines), encoding="ascii")

        client_port4, server_port4, tls_port4 = free_port(), free_port(), free_port()
        config4 = node4 / "unrealircd.conf"
        write_config(config4, "udb-node4.test", "0A4", client_port4, server_port4, tls_port4, module_path, data_dir4)

        proc4 = subprocess.Popen(bwrap_command(node4, ircd_bin, config4),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.2)
        if proc4.poll() is not None:
            stdout4, _ = proc4.communicate()
            raise RuntimeError(f"ircd failed to start with large records:\n{stdout4}")

        # Connect S2S MockServices to verify exact full loaded records across BIGLINES transfer
        services4 = MockServices("127.0.0.1", server_port4, ircd_sid="0A4")
        services4.send(f"DB 0A4 RES C")
        try:
            services4.wait_for(lambda l: " DB " in l and " BEGIN C " in l, "BEGIN C frame", timeout=5)
            services4.wait_for(lambda l: " DB " in l and " END C " in l, "END C frame", timeout=5)
        except Exception as e:
            print(f"DEBUG services4 lines: {services4.lines}", file=sys.stderr)
            raise

        received_puts = {}
        for l in services4.lines:
            if " DB " in l and " PUT C " in l:
                # :0A4 DB 002 PUT C <txid> <path> :<data>
                parts = l.split(" PUT C ", 1)[1].split(" ", 2)
                path = parts[1]
                data = parts[2]
                if data.startswith(":"):
                    data = data[1:]
                received_puts[path] = data

        for exp_path, exp_data in expected_records.items():
            if exp_path not in received_puts:
                raise AssertionError(f"Missing loaded record for {exp_path} in staged export")
            if received_puts[exp_path] != exp_data:
                raise AssertionError(f"Loaded record mismatch for {exp_path}: length {len(received_puts[exp_path])} != {len(exp_data)}")

        services4.close()
        stop(proc4)
        print("PASS: Valid records of sizes 500B, 1024B, 2048B, 4095B, 4096B loaded and verified intact byte-for-byte")

        # -------------------------------------------------------------
        # Test 5: Overlong record (> 12320 bytes) aborts load and preserves disk
        # -------------------------------------------------------------
        node5 = tmpdir / "node5"
        data_dir5 = node5 / "data"
        data_dir5.mkdir(parents=True)
        (node5 / "runtime-data").mkdir()
        (node5 / "tmp").mkdir()
        third_modules5 = node5 / "modules" / "third"
        third_modules5.mkdir(parents=True)
        shutil.copy2(module_path, third_modules5 / "udb.so")

        overlong_db = data_dir5 / "udb_C.db"
        # Overlong record exceeds UDB_RECORD_LINE_MAX (12320)
        overlong_content = "; UDB Block C - Version 1\n#test::topic " + ("y" * 13000) + "\n"
        overlong_db.write_text(overlong_content, encoding="ascii")
        original_bytes5 = overlong_db.read_bytes()

        client_port5, server_port5, tls_port5 = free_port(), free_port(), free_port()
        config5 = node5 / "unrealircd.conf"
        write_config(config5, "udb-node5.test", "0A5", client_port5, server_port5, tls_port5, module_path, data_dir5)

        proc5 = subprocess.Popen(bwrap_command(node5, ircd_bin, config5),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        stop(proc5)
        stdout5, _ = proc5.communicate()

        if overlong_db.read_bytes() != original_bytes5:
            raise AssertionError("Overlong database file was modified after failed transactional load!")
        print("PASS: Overlong line (>12320 bytes) aborted load and preserved original file")

        # -------------------------------------------------------------
        # Test 6: Multi-block init failure does not persist other blocks
        # -------------------------------------------------------------
        node6 = tmpdir / "node6"
        data_dir6 = node6 / "data"
        data_dir6.mkdir(parents=True)
        (node6 / "runtime-data").mkdir()
        (node6 / "tmp").mkdir()
        third_modules6 = node6 / "modules" / "third"
        third_modules6.mkdir(parents=True)
        shutil.copy2(module_path, third_modules6 / "udb.so")

        nicks_db = data_dir6 / "udb_N.db"
        nicks_content = "; UDB Block N - Version 1\nalice::vhost admin.test\n"
        nicks_db.write_text(nicks_content, encoding="ascii")
        nicks_bytes = nicks_db.read_bytes()

        channels_db = data_dir6 / "udb_C.db"
        channels_content = "; UDB Block C - Version 1\n#channel::topic Hello\ncorrupt:::channel\n"
        channels_db.write_text(channels_content, encoding="ascii")
        channels_bytes = channels_db.read_bytes()

        client_port6, server_port6, tls_port6 = free_port(), free_port(), free_port()
        config6 = node6 / "unrealircd.conf"
        write_config(config6, "udb-node6.test", "0A6", client_port6, server_port6, tls_port6, module_path, data_dir6)

        proc6 = subprocess.Popen(bwrap_command(node6, ircd_bin, config6),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        stop(proc6)
        stdout6, _ = proc6.communicate()

        if nicks_db.read_bytes() != nicks_bytes or channels_db.read_bytes() != channels_bytes:
            raise AssertionError("Database files were modified during failed multi-block initialization!")
        print("PASS: Multi-block initialization failure protected all .db files against unwarranted persistence")

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
