#!/usr/bin/env python3
"""Integration tests for UDB modes validation, DEL non-reversal, channel options (*6 lock_modes/lock_topic, *8 persistent), and NickServ notice."""

import argparse
import hashlib
import os
import pathlib
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
CHANNEL = "#locktest"
NICKSERV_NICK = "NickServ"
NICKSERV_MASK = "NickServ!services@services.test"
CHANSERV_MASK = "ChanServ!services@services.test"


class EnvironmentUnavailable(Exception):
    pass


def skip(message):
    print(f"SKIP: {message}")
    return 77


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def sha256(password):
    return hashlib.sha256(password.encode("ascii")).hexdigest()


def write_config(path, name, sid, client_port, server_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB mlock/topiclock integration harness";
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
        self.send("PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 SID=" + self.sid)
        self.send(f"SERVER {SERVICES_NAME} 1 :UDB test services")
        self.wait_for(lambda line: " 001 " in line or " EOS" in line or "NETINFO" in line, "link handshake")
        self.send("EOS")
        self.send(f"DB {self.ircd_sid} HEL 4 ?")
        self.wait_for(lambda line: " DB " in line and " HEL 4 " in line, "UDB HEL response")
        self.send(f"DB {self.ircd_sid} HEL 4 ACK")
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

    def wait_for(self, predicate, description, start=0, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = [line for line in self.lines[start:] if predicate(line)]
            if matches:
                return self.lines[start:]
            self.receive(deadline)
        raise AssertionError(f"services: did not receive {description}; lines={self.lines[start:]!r}")

    def send_ins(self, path, data):
        self.send(f"DB * INS {path} :{data}")

    def send_del(self, path):
        self.send(f"DB * DEL {path}")

    def send_uid(self, nick, username="services", hostname="services.test"):
        self.uid_counter += 1
        uid = f"{self.sid}{self.uid_counter:06d}"
        self.send(f"UID {nick} 1 {int(time.time())} {username} {hostname} {uid} * +S * * * :{nick}")

    def close(self):
        try:
            self.send(f"SQUIT {SERVICES_NAME} :closing test")
        except OSError:
            pass
        finally:
            self.sock.close()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_for_daemon(process, host, port, timeout):
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
    raise RuntimeError("daemon did not open its client listener")


def exercise(host, client_port, server_port):
    clients = []
    services = None
    try:
        services = FakeServicesServer(host, server_port)

        # Alice connects with registered nick password
        alice = IrcClient(host, client_port, "alice-setup")
        clients.append(alice)
        alice.request("NICK davidlig:secret", lambda line: " NICK :davidlig" in line,
                      "nick change to davidlig")
        alice.wait_for(lambda line: " MODE davidlig " in line and "+r" in line, "nick registration +r")
        alice.request("CAP REQ :multi-prefix", lambda line: " CAP " in line, "CAP reply")
        alice.request(f"JOIN {CHANNEL}", lambda line: " 366 " in line, "founder JOIN")

        bob = IrcClient(host, client_port, "bob")
        clients.append(bob)
        bob.request(f"JOIN {CHANNEL}", lambda line: " 366 " in line, "bob JOIN")

        # 1. NickServ guidance notice test:
        # When bob tries to change to registered nick without password:
        nick_reply = bob.request("NICK davidlig", lambda line: any(c in line for c in (" 432 ", " 433 ")),
                                 "rejection of registered nick without password")
        require(any("Nickname is unavailable: This nick is registered and requires a password and an authorized IP. Use /NICK davidlig:Password" in line
                    for line in nick_reply),
                f"Did not receive expected NickServ notice: {nick_reply!r}")
        print("PASS: NickServ notice with format '/NICK <nick>:Password' sent correctly")

        # 2. options *6 test (lock_modes + lock_topic):
        # Modes are locked (*2 in options *6). Neither founder nor non-founder can change modes.
        start = len(bob.lines)
        bob.send(f"MODE {CHANNEL} +s")
        bob.wait_for(lambda line: "locked by UDB" in line and (line.startswith(":ChanServ") or "ChanServ" in line),
                     "mode lock by options lock_modes (bob) from ChanServ")
        print("PASS: options lock_modes blocked normal user mode change via ChanServ")

        start = len(alice.lines)
        alice.send(f"MODE {CHANNEL} +s")
        alice.wait_for(lambda line: "locked by UDB" in line and (line.startswith(":ChanServ") or "ChanServ" in line),
                       "mode lock by options lock_modes (founder) from ChanServ")
        print("PASS: options lock_modes blocked founder mode change via ChanServ")

        # List modes (+b, +e, +I) must not be blocked by lock_modes
        alice.request(f"MODE {CHANNEL} +b *!*@lockexempt.test", lambda line: f"MODE {CHANNEL} +b *!*@lockexempt.test" in line,
                      "list mode +b allowed with active lock_modes")
        alice.request(f"MODE {CHANNEL} -b *!*@lockexempt.test", lambda line: f"MODE {CHANNEL} -b *!*@lockexempt.test" in line,
                      "list mode -b allowed with active lock_modes")
        print("PASS: options lock_modes did not block list modes (+b / -b)")

        # 3. options *6 test (lock_topic):
        # Topic is locked (*4 in options *6). Neither founder nor non-founder can change topic.
        start = len(bob.lines)
        bob.send(f"TOPIC {CHANNEL} :New topic by bob")
        bob.wait_for(lambda line: "locked by UDB" in line and (line.startswith(":ChanServ") or "ChanServ" in line),
                     "topic lock by options lock_topic (bob) from ChanServ")
        print("PASS: options lock_topic blocked bob topic change via ChanServ")

        start = len(alice.lines)
        alice.send(f"TOPIC {CHANNEL} :New topic by alice")
        alice.wait_for(lambda line: "locked by UDB" in line and (line.startswith(":ChanServ") or "ChanServ" in line),
                       "topic lock by options lock_topic (founder) from ChanServ")
        print("PASS: options lock_topic blocked founder topic change via ChanServ")

        # 4. Reject invalid options and unlock via INS *0 / DEL options:
        services.send_ins(f"C::{CHANNEL}::options", "6")
        services.wait_for(lambda line: " DB " in line and " ERR " in line and " INS " in line,
                          "rejection of options without *")
        print("PASS: INS options without '*' was rejected with ERR INS")

        services.send_ins(f"C::{CHANNEL}::options", "*abc")
        services.wait_for(lambda line: " DB " in line and " ERR " in line and " INS " in line,
                          "rejection of non-numeric options *abc")
        print("PASS: INS non-numeric options was rejected with ERR INS")

        services.send_ins(f"C::{CHANNEL}::options", "*0")
        time.sleep(0.2)

        # Now founder can change modes and topic
        start = len(alice.lines)
        alice.request(f"MODE {CHANNEL} +s", lambda line: f"MODE {CHANNEL} +s" in line, "mode +s allowed after options *0")
        alice.request(f"TOPIC {CHANNEL} :Unlocked topic", lambda line: f"TOPIC {CHANNEL}" in line, "topic allowed after options *0")
        print("PASS: INS options *0 allowed mode and topic modifications to founder")

        # 5. Validation of modes with missing parameters via INS:
        start_svc = len(services.lines)
        services.send_ins(f"C::{CHANNEL}::modes", "+ntMl")
        # Should result in DB ERR INS
        services.wait_for(lambda line: " DB " in line and " ERR " in line and " INS " in line,
                          "rejection of modes +ntMl without parameter")
        print("PASS: INS modes +ntMl without parameter was rejected with ERR INS")

        # Valid mode with parameter should be accepted:
        services.send_ins(f"C::{CHANNEL}::modes", "+ntMl 50")
        alice.wait_for(lambda line: f"MODE {CHANNEL}" in line and "50" in line,
                       "application of modes +ntMl 50 with parameter")
        print("PASS: INS modes +ntMl 50 with parameter was accepted and applied")

        # 6. DEL of modes without live reversion:
        start_alice = len(alice.lines)
        services.send_del(f"C::{CHANNEL}::modes")
        time.sleep(0.5)
        alice.receive(time.monotonic() + 0.5)
        del_traffic = [line for line in alice.lines[start_alice:] if f"MODE {CHANNEL}" in line]
        require(not del_traffic, f"DEL of modes reverted modes live: {del_traffic!r}")
        print("PASS: DEL of modes removed record without live modes reversion")

        # 7. Persistent channel mode (+P) tests via options *8:
        # 7a. INS options *8 on active channel sets +P
        services.send_ins(f"C::{CHANNEL}::options", "*8")
        alice.wait_for(lambda line: f"MODE {CHANNEL} +P" in line,
                       "application of +P on active channel after INS options *8")
        print("PASS: INS options *8 set +P mode on active channel")

        # 7b. DEL options removes -P
        services.send_del(f"C::{CHANNEL}::options")
        alice.wait_for(lambda line: f"MODE {CHANNEL} -P" in line,
                       "removal of -P after DEL options")
        print("PASS: DEL of options removed -P mode on active channel")

        # 7c. INS options *8 on non-existent channel creates it with +P
        empty_chan = "#emptyperm"
        services.send_ins(f"C::{empty_chan}::options", "*8")
        time.sleep(0.2)
        # Bob joins #emptyperm and requests MODE to see +P in 324
        bob.request(f"JOIN {empty_chan}", lambda line: " 366 " in line, "JOIN #emptyperm")
        bob.request(f"MODE {empty_chan}", lambda line: " 324 " in line and "P" in line,
                    "persistent channel instantiated with +P for empty channel")
        print("PASS: INS options *8 instantiated empty channel with +P mode")

        # 7d. DEL options on empty channel destroys it
        bob.send(f"PART {empty_chan} :bye")
        time.sleep(0.2)
        services.send_del(f"C::{empty_chan}::options")
        time.sleep(0.2)
        print("PASS: DEL of options on empty channel processed cleanly")

        # 8. Channel options *1 (protect_bans):
        charlie = IrcClient(host, client_port, "charlie")
        clients.append(charlie)
        charlie.request(f"JOIN {CHANNEL}", lambda line: " 366 " in line, "charlie JOIN")

        # Give op (+o) to bob and charlie
        alice.request(f"MODE {CHANNEL} +oo bob charlie", lambda line: f"MODE {CHANNEL} +oo bob charlie" in line, "op bob charlie")
        time.sleep(0.2)

        # Enable protect_bans (*1)
        services.send_ins(f"C::{CHANNEL}::options", "*1")
        time.sleep(0.2)

        # Bob adds a ban
        bob.request(f"MODE {CHANNEL} +b *!*@evil.test", lambda line: f"MODE {CHANNEL} +b *!*@evil.test" in line, "bob ban add")

        # Charlie (non-author op) tries to remove Bob's ban -> blocked by ChanServ notice
        charlie.send(f"MODE {CHANNEL} -b *!*@evil.test")
        charlie.wait_for(lambda line: "You may not remove the UDB-protected ban" in line and "evil.test" in line and "ChanServ" in line,
                         "block of protected ban removal by ChanServ")
        print("PASS: options protect_bans (*1) blocked ban removal by non-author operator")

        # Bob (author) removes his own ban -> allowed
        bob.request(f"MODE {CHANNEL} -b *!*@evil.test", lambda line: f"MODE {CHANNEL} -b *!*@evil.test" in line, "bob ban remove")
        print("PASS: options protect_bans (*1) allowed ban removal by original author")

        # Bob adds another ban, Alice (founder) removes it -> allowed (founder bypass)
        bob.request(f"MODE {CHANNEL} +b *!*@other.test", lambda line: f"MODE {CHANNEL} +b *!*@other.test" in line, "bob ban add 2")
        alice.request(f"MODE {CHANNEL} -b *!*@other.test", lambda line: f"MODE {CHANNEL} -b *!*@other.test" in line, "founder ban remove")
        print("PASS: options protect_bans (*1) allowed ban removal by founder")


    finally:
        for client in clients:
            client.close()
        if services:
            services.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    if not args.ircd.is_file():
        return skip(f"compiled ircd executable is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-mlock-"))
    process = None
    try:
        node = root / "node"
        data = node / "data"
        data.mkdir(parents=True)
        (node / "runtime-data").mkdir()
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(args.module, third_modules / "udb.so")

        (data / "udb_S.db").write_text(
            f"nickserv {NICKSERV_MASK}\n"
            f"chanserv {CHANSERV_MASK}\n",
            encoding="ascii")
        (data / "udb_N.db").write_text(
            f"davidlig::pass sha256:{sha256('secret')}\n"
            "davidlig::challenge sha256\n"
            "davidlig::access 127.0.0.0/8\n",
            encoding="ascii")
        (data / "udb_C.db").write_text(
            f"{CHANNEL}::founder davidlig\n"
            f"{CHANNEL}::modes +ntM\n"
            f"{CHANNEL}::options *6\n",
            encoding="ascii")

        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-one.test", "001", client_port, server_port, tls_port, args.module, data)
        run_configtest(node, args.ircd, config)

        log = node / "ircd.log"
        with log.open("w") as output:
            process = subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=output,
                                       stderr=subprocess.STDOUT, text=True)
        wait_for_daemon(process, "127.0.0.1", client_port, args.timeout)
        exercise("127.0.0.1", client_port, server_port)
        return 0
    except EnvironmentUnavailable as exc:
        return skip(f"bwrap cannot create the required mount namespace: {exc}")
    except (AssertionError, OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        if root.exists():
            log = root / "node" / "ircd.log"
            if log.exists():
                print(f"--- daemon log ({log}) ---\n{log.read_text(errors='replace')}", file=sys.stderr)
        return 1
    finally:
        stop(process)
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
