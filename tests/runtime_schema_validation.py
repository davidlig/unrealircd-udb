#!/usr/bin/env python3
"""Integration tests for UDB declarative schema validation across all database blocks."""

import argparse
import base64
import hashlib
import os
import pathlib
import select
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

SERVICES_NAME = "udb-svc.test"
SERVICES_SID = "002"
IRCD_SID = "001"
LINK_PASSWORD = "udb-svc-link-password"
EMPTY_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
CHANNEL = "#schematest"


class EnvironmentUnavailable(Exception):
    pass


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_for_db_text(db, needle, description, timeout=15.0):
    """Poll a block snapshot until the authorized mutation is durably visible.

    CI runners can stall between accepting an INS and the fsync-backed snapshot
    rename, so a fixed sleep races the persistence commit. The predicate is
    unchanged, so a wider window can never turn a real failure into a pass.
    """
    deadline = time.monotonic() + timeout
    content = ""
    while time.monotonic() < deadline:
        content = db.read_text(encoding="ascii") if db.exists() else ""
        if needle in content:
            return content
        time.sleep(0.05)
    raise AssertionError(f"{description}: {needle!r} missing from {db.name}:\n{content}")


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir, extra_modules=()):
    extra_load = "".join(f'loadmodule "{entry}";\n' for entry in extra_modules)
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB schema validation integration harness";
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
class clients {{ pingfreq 60; maxclients 100; sendq 1M; recvq 8000; }}
class servers {{ pingfreq 60; connfreq 6; maxclients 4; sendq 20M; }}
allow {{ mask "127.0.0.1"; class clients; maxperip 100; }}
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
{extra_load}udb {{
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


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


class IrcClient:
    def __init__(self, host, port, nick):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=3)
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
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                raise AssertionError(f"{self.nick}: server closed the connection")
            self.buffer += data.decode(errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, start=0, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = [line for line in self.lines[start:] if predicate(line)]
            if matches:
                return self.lines[start:]
            self.receive(deadline)
        raise AssertionError(f"{self.nick}: did not receive {description}; lines={self.lines[start:]!r}")

    def request(self, command, terminator, description):
        start = len(self.lines)
        self.send(command)
        return self.wait_for(terminator, description, start)

    def close(self):
        try:
            self.send("QUIT :test complete")
        except OSError:
            pass
        finally:
            self.sock.close()


class FakeServicesServer:
    def __init__(self, host, port):
        self.sid = SERVICES_SID
        self.ircd_sid = IRCD_SID
        self.uid_counter = 0
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
        self.send(f"DB {self.ircd_sid} HEL 4 ? 0000000000000001 OCL")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK ? 0000000000000001 OCL")
        for b in ('N', 'C', 'I', 'S', 'L', 'K'):
            self.send(f"DB {self.ircd_sid} INF 1 {b} {EMPTY_SHA256} 0 0")
        time.sleep(0.2)
        self.send_uid("NickServ")
        self.send_uid("ChanServ")

    def send(self, command):
        if not command.startswith(":"):
            command = ":" + self.sid + " " + command
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def receive(self, deadline):
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return
            if not data:
                raise AssertionError("services: server closed connection")
            self.buffer += data.decode(errors="replace")
            while "\r\n" in self.buffer:
                line, self.buffer = self.buffer.split("\r\n", 1)
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                elif line:
                    self.lines.append(line)

    def wait_for(self, predicate, description, start=0, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = [line for line in self.lines[start:] if predicate(line)]
            if matches:
                return self.lines[start:]
            self.receive(deadline)
        raise AssertionError(f"services: did not receive {description}; lines={self.lines[start:]!r}")

    def send_uid(self, nick):
        self.uid_counter += 1
        uid = f"{self.sid}AAAA{self.uid_counter:02d}"
        self.send(f"UID {nick} 1 {int(time.time())} +ioSq {nick} services.test {nick}.services.test {uid} 0 0 :UDB Services")
        return uid

    def send_ins(self, path, data):
        self.seq = getattr(self, "seq", 0) + 1
        self.send(f"DB * INS 0000000000000001 {self.seq} {path} :{data}")

    def send_del(self, path):
        self.seq = getattr(self, "seq", 0) + 1
        self.send(f"DB * DEL 0000000000000001 {self.seq} {path}")

    def close(self):
        try:
            self.send("SQUIT " + SERVICES_NAME + " :test complete")
        except OSError:
            pass
        finally:
            self.sock.close()


def test_clone_limits_int_max_and_one_over(services, data_dir):
    accepted = "2147483647"
    rejected = "2147483648"
    paths = ("S::clones", "I::127.0.0.1::clones")

    for path in paths:
        services.send_ins(path, "*" + accepted)
    for path in paths:
        db = data_dir / ("udb_S.db" if path.startswith("S::") else "udb_I.db")
        wait_for_db_text(db, f"{path[3:]} *{accepted}", f"{path} at INT_MAX was not persisted exactly")

    for path in paths:
        start = len(services.lines)
        services.send_ins(path, "*" + rejected)
        services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                          f"rejection of {path} above INT_MAX", start=start)
        db = data_dir / ("udb_S.db" if path.startswith("S::") else "udb_I.db")
        require(rejected not in db.read_text(encoding="ascii"),
                f"{path} above INT_MAX changed the persisted policy")
    print("PASS: clone limits accept INT_MAX and reject INT_MAX + 1 without replacement")


def test_secret_mutation_log_redaction(services, process):
    secret_key = "a1" * 32
    secret_hash = "sha256:" + ("b2" * 32)
    secret_modes = "+ntk s3cr3tchannelkey"
    mutations = (
        ("S::encryption_key", secret_key),
        ("N::secretlogger::pass", secret_hash),
        ("C::#secretlogger::modes", secret_modes),
    )
    for path, value in mutations:
        services.send_ins(path, value)
    time.sleep(0.5)
    output = ""
    while select.select([process.stdout], [], [], 0)[0]:
        output += os.read(process.stdout.fileno(), 65536).decode(errors="replace")
    require(output, "secret mutation test did not capture daemon diagnostics")
    for secret in (secret_key, secret_hash, "s3cr3tchannelkey"):
        require(secret not in output, "secret mutation value appeared in daemon diagnostics")
    require(
        "S::encryption_key" in output and "N::secretlogger::pass" in output and "C::#secretlogger::modes" in output,
        "redacted mutation diagnostics lost their paths",
    )
    print("PASS: mutation diagnostics retain paths while redacting every secret value")


def test_secret_mutation_log_redaction_encoded(services, process, data_dir):
    secret_key = "c3" * 32
    secret_hash = "sha256:" + ("d4" * 32)
    encoded_mutations = (
        ("S::encryption%5Fkey", secret_key),
        ("N::encodedtest::%70ass", secret_hash),
        ("N::encodedtest2::p%61ss", secret_hash),
    )
    for path, value in encoded_mutations:
        start = len(services.lines)
        services.send_ins(path, value)
        services.wait_for(lambda line: " DB " in line and " ERR " in line and " INS " in line,
                          f"rejection of non-canonical %XX path {path}", start=start)

    time.sleep(0.3)
    output = ""
    while select.select([process.stdout], [], [], 0)[0]:
        output += os.read(process.stdout.fileno(), 65536).decode(errors="replace")
    for secret in (secret_key, secret_hash, "c3" * 16, "d4" * 16):
        require(secret not in output, "secret value from rejected non-canonical path appeared in daemon diagnostics")

    for db_name in ("udb_S.db", "udb_N.db", "udb_C.db"):
        db_path = data_dir / db_name
        if db_path.exists():
            content = db_path.read_text(encoding="ascii", errors="replace")
            for secret in (secret_key, secret_hash):
                require(secret not in content, f"secret value was persisted to {db_name} despite rejection")
    print("PASS: non-canonical %XX-encoded paths are strictly rejected with ERR INS without persisting or leaking secrets")


def test_line_mask_component_and_native_boundaries(services, data_dir):
    user = "u" * 127
    host = "h" * 127
    valid_path = f"K::G::{user}@{host}::reason"
    services.send_ins(valid_path, "boundary mask")
    db = data_dir / "udb_K.db"
    wait_for_db_text(db, valid_path[3:], "exact native mask boundary was not persisted")

    for invalid_mask in (("u" * 128) + "@host.test", "user@" + ("h" * 128)):
        start = len(services.lines)
        services.send_ins(f"K::G::{invalid_mask}::reason", "must reject")
        services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                          "rejection of over-capacity line-mask component", start=start)
        require(invalid_mask not in db.read_text(encoding="ascii"),
                "over-capacity line mask was persisted after rejection")
    print("PASS: line masks preserve exact native boundaries and reject one-over components")


def test_block_k_absolute_expiry_schema(services, data_dir):
    db = data_dir / "udb_K.db"
    future = int(time.time()) + 3600
    services.send_ins("K::G::*@expires.test::reason", "absolute expiry")
    services.send_ins("K::G::*@expires.test::expires", f"*{future}")
    content = wait_for_db_text(db, f"G::*@expires.test::expires *{future}",
                               "absolute K expires value was not persisted exactly")
    require("duration" not in content, "legacy K duration unexpectedly persisted")

    for path, value, label in (
        ("K::G::*@expires.test::duration", "*60", "legacy duration"),
        ("K::G::*@expires.test::expires", "*0", "zero expires"),
        ("K::G::*@expires.test::expires", "*18446744073709551616", "overflow expires"),
        ("K::Z::host.invalid::reason", "bad Z", "hostname Z-line"),
        ("K::F::raw-pattern::reason", "raw", "raw spamfilter alias"),
        ("K::F::b64%3AcmVnZXg=::type", "c", "legacy spamfilter target key"),
        ("K::F::b64%3AcmVnZXg=::action", "set", "config-only spamfilter action"),
    ):
        before = db.read_text(encoding="ascii")
        start = len(services.lines)
        services.send_ins(path, value)
        services.wait_for(lambda line: " DB " in line and " ERR " in line and " INS " in line and " K" in line,
                          f"rejection of {label}", start=start)
        require(before == db.read_text(encoding="ascii"), f"{label} changed udb_K.db despite rejection")
    print("PASS: Block K accepts absolute expires only and rejects legacy/invalid expiry, Z, raw/legacy F aliases, and config-only action")


def test_block_k_canonical_profiles(services, data_dir):
    """Exercise K identity admission without relying on daemon internals."""
    db = data_dir / "udb_K.db"

    def encoded(pattern):
        return "b64%3A" + base64.b64encode(pattern.encode("ascii")).decode("ascii")

    # No direct reason alias and no G shorthand can create a second identity.
    for path, value, label in (
        ("K::G::example.test", "legacy direct reason", "direct G reason"),
        ("K::G::example.test::reason", "shorthand", "G shorthand"),
        ("K::Z::2001%3A0db8%3A0%3A0%3A0%3A0%3A0%3A1::reason", "alias", "noncanonical IPv6"),
        ("K::Z::2001%3Adb8%3A%3A1/32::reason", "host bits", "CIDR host bits"),
    ):
        before = db.read_text(encoding="ascii") if db.exists() else ""
        start = len(services.lines)
        services.send_ins(path, value)
        services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                          f"rejection of {label}", start=start)
        require((db.read_text(encoding="ascii") if db.exists() else "") == before,
                f"{label} changed persistent K state")

    # Foo and foo are different regex bytes and must remain distinct tree keys.
    for pattern in ("Foo", "foo"):
        root = f"K::F::{encoded(pattern)}"
        services.send_ins(root + "::match-type", "regex")
        services.send_ins(root + "::targets", "c")
        services.send_ins(root + "::action", "warn")
        services.send_ins(root + "::ban-time", "*60")
        services.send_ins(root + "::reason", f"case {pattern}")
    content = wait_for_db_text(db, f"{encoded('foo')}::reason case foo",
                               "the complete foo F profile was not durably persisted")
    require(encoded("Foo") in content,
            "case-distinct Base64 F patterns did not coexist in udb_K.db")
    require("::targets c" in content and "::match-type regex" in content and "::ban-time *60" in content,
            "complete F profile was not persisted with explicit schema")

    for path, value, label in (
        (f"K::F::{encoded('targets')}::targets", "pc", "noncanonical target order"),
        (f"K::F::{encoded('targets')}::targets", "cc", "duplicate target"),
        (f"K::F::{encoded('targets')}::match-type", "implicit", "unknown match type"),
        (f"K::F::{encoded('targets')}::ban-time", "*0", "zero ban-time"),
    ):
        before = db.read_text(encoding="ascii")
        start = len(services.lines)
        services.send_ins(path, value)
        services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                          f"rejection of {label}", start=start)
        require(db.read_text(encoding="ascii") == before, f"{label} changed udb_K.db")
    print("PASS: canonical K masks and explicit case-sensitive F profiles reject aliases and preserve identity")


def test_block_k_expiry_gc(services, data_dir, client_port):
    path = "K::G::*@127.0.0.1"
    db = data_dir / "udb_K.db"
    future = int(time.time()) + 60
    services.send_ins(path + "::reason", "expiry GC")
    services.send_ins(path + "::expires", f"*{future}")
    content = wait_for_db_text(db, f"G::*@127.0.0.1::expires *{future}",
                               "temporary K profile was not in active persisted state")

    # A non-expiry field change must not extend the absolute expiry.
    services.send_ins(path + "::reason", "expiry GC changed reason")
    content = wait_for_db_text(db, "G::*@127.0.0.1::reason expiry GC changed reason",
                               "non-expiry K profile update was not persisted")
    require(f"G::*@127.0.0.1::expires *{future}" in content,
            "reason update changed the absolute expiry")

    # Expire now. The follower cannot edit authoritative persistence; it asks
    # the configured authority, whose canonical DEL removes the whole profile.
    start = len(services.lines)
    services.send_ins(path + "::expires", "*1")
    services.wait_for(lambda line: " DB " in line and " EXP " in line and path in line,
                      "EXP request for expired K profile", start=start, timeout=5)
    services.send_del(path)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and "G::*@127.0.0.1" in db.read_text(encoding="ascii"):
        time.sleep(0.1)
    require("G::*@127.0.0.1" not in db.read_text(encoding="ascii"),
            "canonical expiry DEL left K subtree residue in udb_K.db")

    # The runtime TKL was removed before EXP and remains absent after the DEL:
    # a matching local client can register immediately.
    client = IrcClient("127.0.0.1", client_port, "afterexpiry")
    client.close()
    print("PASS: expired K profile requested EXP, canonical DEL removed the full persisted subtree, and no TKL resurrected")


def test_channel_mode_parameter_capacity_atomic(services, data_dir):
    params12 = [str(index + 10) for index in range(12)]
    value12 = " ".join(["+" + ("l" * len(params12)), *params12])
    services.send_ins(f"C::{CHANNEL}::modes", value12)
    db = data_dir / "udb_C.db"
    wait_for_db_text(db, value12, "12 mode parameters were not persisted as one record")

    params13 = [str(index + 10) for index in range(13)]
    value13 = " ".join(["+" + ("l" * len(params13)), *params13])
    start = len(services.lines)
    services.send_ins(f"C::{CHANNEL}::modes", value13)
    services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                      "atomic rejection of 13 channel mode parameters", start=start)
    require(value13 not in db.read_text(encoding="ascii"),
            "13 mode parameters were partially persisted instead of rejected atomically")
    print("PASS: channel modes accept 12 parameters and atomically reject 13")


def test_dynamic_hash_resize_runtime(services, data_dir, process):
    """Exercise the compiled copy-on-write index across the 1536/1537 boundary."""
    profiles = 1537
    db = data_dir / "udb_N.db"

    for index in range(profiles):
        services.send_ins(f"N::hash{index:04d}::forbid", "resize fixture")

    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        # Each authorized mutation is logged by this harness; drain the pipe so
        # diagnostics cannot stall the daemon while the burst is processed.
        while select.select([process.stdout], [], [], 0)[0]:
            os.read(process.stdout.fileno(), 65536)
        content = db.read_text(encoding="ascii") if db.exists() else ""
        if content.count("::forbid resize fixture") >= profiles:
            break
        time.sleep(0.1)
    else:
        raise AssertionError("1537 root profiles did not persist across the hash resize boundary")

    for index in (0, 768, 1536):
        require(f"hash{index:04d}::forbid resize fixture" in content,
                f"root profile hash{index:04d} was lost during runtime rehash")

    services.send_del("N::hash0768")
    services.send_ins("N::hashnew::forbid", "post-rehash insert")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        while select.select([process.stdout], [], [], 0)[0]:
            os.read(process.stdout.fileno(), 65536)
        content = db.read_text(encoding="ascii")
        if "hash0768::" not in content and "hashnew::forbid post-rehash insert" in content:
            break
        time.sleep(0.1)
    else:
        raise AssertionError("runtime hash lookup/delete/insert did not converge after rehash")
    require(content.count("::forbid resize fixture") == profiles - 1,
            "runtime rehash retained a deleted profile or duplicated a root profile")
    print("PASS: compiled UDB rehashed 1536->1537 roots and preserved lookup, DEL, and new INS behavior")


def run_tests(ircd_bin, keep=False):
    tmpdir = tempfile.mkdtemp(prefix="udb-schema-test-")
    node = pathlib.Path(tmpdir)
    proc = None

    try:
        data_dir = node / "runtime-data"
        runtime_data = node / "runtime-data"
        tmp_dir = node / "tmp"
        mods_third = node / "modules" / "third"
        for d in (data_dir, runtime_data, tmp_dir, mods_third):
            d.mkdir(parents=True, exist_ok=True)

        module_so = find_module_path()
        shutil.copy2(module_so, mods_third / "udb.so")

        client_port = free_port()
        server_port = free_port()
        tls_port = free_port()
        config = node / "unrealircd.conf"
        write_config(config, "ircd.test", IRCD_SID, client_port, server_port, tls_port,
                     mods_third / "udb.so", str(data_dir))

        run_configtest(node, ircd_bin, config)

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        if proc.poll() is not None:
            stdout, _ = proc.communicate()
            raise RuntimeError(f"ircd exited immediately:\n{stdout}")

        services = FakeServicesServer("127.0.0.1", server_port)

        # -------------------------------------------------------------
        # Test 1: Rejection of unknown keys in Block C (Channel)
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::testunknownkey", "valor")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rejection of unknown key testunknownkey in Block C")
        print("PASS: INS of unknown key testunknownkey in Block C was rejected with correlated ERR INS 2")

        services.send_ins(f"C::{CHANNEL}::testkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rejection of unknown key testkey in Block C")
        print("PASS: INS of unknown key testkey in Block C was rejected with correlated ERR INS 2")

        services.send_ins(f"C::{CHANNEL}::testinvalidkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rejection of unknown key testinvalidkey in Block C")
        print("PASS: INS of unknown key testinvalidkey in Block C was rejected with correlated ERR INS 2")

        for path in ("N::removedchallenge::challenge", "N::removedsuspend::suspended",
                     f"C::{CHANNEL}::pass", f"C::{CHANNEL}::challenge", f"C::{CHANNEL}::suspended"):
            start = len(services.lines)
            services.send_ins(path, "sha256")
            services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                              f"rejection of removed schema key {path}", start=start)
        services.send_ins("N::acceptsuspend::suspend", "manual review")
        wait_for_db_text(data_dir / "udb_N.db", "acceptsuspend::suspend manual review",
                         "N::suspend was not accepted by the replacement schema")

        # `forbid` is canonical and exclusive: an INS atomically drops siblings
        # and future sibling inserts are rejected until forbid is deleted.
        services.send_ins("N::forbidtest::pass", "sha256:" + "ab" * 32)
        services.send_ins("N::forbidtest::forbid", "reserved")
        n_db = wait_for_db_text(data_dir / "udb_N.db", "forbidtest::forbid reserved",
                                "INS forbid did not persist an exclusive canonical profile")
        require("forbidtest::pass" not in n_db,
                "INS forbid did not persist an exclusive canonical profile")
        records_header = next((line for line in n_db.splitlines() if line.startswith("; Records: ")), None)
        require(records_header is not None, "N snapshot omitted its Records header")
        logical_records = [line for line in n_db.splitlines() if line and not line.startswith(";")]
        require(int(records_header.rsplit(" ", 1)[1]) == len(logical_records),
                "Records header does not match the exact serialized logical-record count")
        start = len(services.lines)
        services.send_ins("N::forbidtest::vhost", "forbid.example")
        services.wait_for(lambda line: " DB " in line and " ERR INS " in line,
                          "rejection of sibling under forbid", start=start)
        print("PASS: removed schema names reject; suspend and canonical forbid enforce the new N/C contracts")

        # -------------------------------------------------------------
        # Test 2: Rejection of wrong data types in Block C
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::founder", "*123")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rejection of numeric type in founder")
        print("PASS: INS of founder with numeric value was rejected")

        services.send_ins(f"C::{CHANNEL}::options", "noval")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " C" in l,
                          "rejection of string type in options")
        print("PASS: INS of options without numeric '*' format was rejected")

        # -------------------------------------------------------------
        # Test 3: Acceptance of valid keys in Block C
        # -------------------------------------------------------------
        services.send_ins(f"C::{CHANNEL}::founder", "davidlig")
        services.send_ins(f"C::{CHANNEL}::options", "*3")
        services.send_ins(f"C::{CHANNEL}::access::davidlig", "*1")
        time.sleep(0.2)
        print("PASS: INS of valid keys in Block C (founder, options, access) were accepted")

        # -------------------------------------------------------------
        # Test 4: Rejection of invalid nested paths in Block S
        # -------------------------------------------------------------
        services.send_ins("S::#test::testunknownkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rejection of nested path in Block S")
        print("PASS: INS of nested path S::#test::testunknownkey was rejected with correlated ERR INS 2")

        services.send_ins("S::testunknownkey", "ascac")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rejection of unknown key in Block S")
        print("PASS: INS of unknown key testunknownkey in Block S was rejected with correlated ERR INS 2")

        services.send_ins("S::clones", "textvalue")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " S" in l,
                          "rejection of non-numeric clones in Block S")
        print("PASS: INS of clones with text in Block S was rejected")

        # -------------------------------------------------------------
        # Test 5: Acceptance of valid keys in Block S
        # -------------------------------------------------------------
        services.send_ins("S::clones", "*5")
        services.send_ins("S::quit_clones", "Too many connections")
        time.sleep(0.2)
        print("PASS: INS of valid keys in Block S (clones *5, quit_clones) were accepted")

        # -------------------------------------------------------------
        # Test 6: Rejection of unknown keys in Block N, I, L, K
        # -------------------------------------------------------------
        services.send_ins("N::davidlig::testunknownkey", "valor")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " N" in l,
                          "rejection of unknown key in Block N")
        print("PASS: INS of unknown key in Block N was rejected with correlated ERR INS 2")

        services.send_ins("I::127.0.0.1::testunknownkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " I" in l,
                          "rejection of unknown key in Block I")
        print("PASS: INS of unknown key in Block I was rejected with correlated ERR INS 2")

        services.send_ins(f"L::{SERVICES_NAME}::testunknownkey", "*1")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " L" in l,
                          "rejection of unknown key in Block L")
        print("PASS: INS of unknown key in Block L was rejected with correlated ERR INS 2")

        services.send_ins("K::X::*@bad.test::reason", "bad")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " K" in l,
                          "rejection of invalid TKL type in Block K")
        print("PASS: INS of invalid TKL type 'X' in Block K was rejected with correlated ERR INS 2")

        services.send_ins("K::G::*@bad.test::testunknownkey", "bad")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " K" in l,
                          "rejection of unknown subkey in Block K")
        print("PASS: INS of unknown subkey in Block K was rejected with correlated ERR INS 2")

        test_clone_limits_int_max_and_one_over(services, data_dir)
        test_secret_mutation_log_redaction(services, proc)
        test_secret_mutation_log_redaction_encoded(services, proc, data_dir)
        test_line_mask_component_and_native_boundaries(services, data_dir)
        test_block_k_absolute_expiry_schema(services, data_dir)
        test_block_k_canonical_profiles(services, data_dir)
        test_block_k_expiry_gc(services, data_dir, client_port)
        test_channel_mode_parameter_capacity_atomic(services, data_dir)
        test_dynamic_hash_resize_runtime(services, data_dir, proc)

        # -------------------------------------------------------------
        # Test 6b: Spamfilter regex pattern length limits (3071, 3072, 3073 bytes)
        # -------------------------------------------------------------
        # Canonical Base64 patterns preserve the 3072-byte native limit.
        pat3071 = "a" * 3071
        pat3072 = "b" * 3072
        pat3073 = "c" * 3073
        b64_3071 = "b64%3A" + base64.b64encode(pat3071.encode()).decode()
        b64_3072 = "b64%3A" + base64.b64encode(pat3072.encode()).decode()
        b64_3073 = "b64%3A" + base64.b64encode(pat3073.encode()).decode()
        services.send_ins(f"K::F::{b64_3071}::reason", "partial")
        services.send_ins(f"K::F::{b64_3072}::reason", "partial")
        time.sleep(0.1)
        services.send_ins(f"K::F::{b64_3073}::reason", "too long")
        services.wait_for(lambda l: " DB " in l and " ERR " in l and " INS " in l and " K" in l,
                          "rejection of spamfilter pattern exceeding 3072 bytes")
        print("PASS: canonical Base64 Spamfilter pattern lengths: 3071/3072 accepted, 3073 rejected")

        services.close()
        stop(proc)
        proc = None

        # -------------------------------------------------------------
        # Test 7: File parsing on boot rejects unknown/corrupted keys transactionally
        # -------------------------------------------------------------
        db_c = data_dir / "udb_C.db"
        db_c.write_text("""; UDB Block C - Version 1
; Saved: 1787715840
; Records: 5
#channel::testunknownkey *1
#channel::founder davidlig
#channel::testinvalidkey ascac
#channel::options *3
#channel::testkey *1
""", encoding="ascii")
        orig_bytes = db_c.read_bytes()

        proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        stop(proc)
        stdout, _ = proc.communicate()

        if "Malformed persisted record in block C" not in stdout and "Failed to initialize database engine" not in stdout:
            raise RuntimeError(f"Expected transactional abort error in stdout, got:\n{stdout}")
        if db_c.read_bytes() != orig_bytes:
            raise AssertionError("Corrupted udb_C.db was overwritten after failed load!")
        proc = None
        print("PASS: Server startup aborted transactionally on corrupted records in udb_C.db")

        # -------------------------------------------------------------
        # Test 8: C::modes reserved letters are rejected on file load too
        # -------------------------------------------------------------
        for reserved_modes in ("+ntr", "+ntP", "+ntovh davidlig davidlig davidlig"):
            db_c.write_text(f"""; UDB Block C - Version 1
; Saved: 1787715840
; Records: 2
#reserved::founder davidlig
#reserved::modes {reserved_modes}
""", encoding="ascii")
            orig_bytes = db_c.read_bytes()

            proc = subprocess.Popen(bwrap_command(node, ircd_bin, config),
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            time.sleep(1.0)
            stop(proc)
            stdout, _ = proc.communicate()

            if "Malformed persisted record in block C" not in stdout and "Failed to initialize database engine" not in stdout:
                raise RuntimeError(f"Expected load rejection for C::modes {reserved_modes!r}, got:\n{stdout}")
            if db_c.read_bytes() != orig_bytes:
                raise AssertionError(f"udb_C.db with reserved modes {reserved_modes!r} was overwritten after failed load!")
            proc = None
        print("PASS: reserved C::modes letters (+r, +P, member ranks) abort startup transactionally")

    finally:
        if proc:
            stop(proc)
        if not keep:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDB schema validation integration tests")
    parser.add_argument("--ircd", default=str(DEFAULT_IRCD), help="Path to unrealircd binary")
    parser.add_argument("--keep", action="store_true", help="Keep temporary test directories")
    args = parser.parse_args()

    if not os.path.isfile(args.ircd):
        sys.exit(skip(f"UnrealIRCd binary not found at {args.ircd}"))

    try:
        run_tests(args.ircd, keep=args.keep)
    except EnvironmentUnavailable as err:
        sys.exit(skip(str(err)))
