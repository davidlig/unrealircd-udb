#!/usr/bin/env python3
"""Isolated one-node UDB nick and channel runtime integration harness."""

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

from udb_state_seed import seed_block, seed_ready_state


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


def write_config(path, name, sid, client_port, tls_port, module, dbdir):
    path.write_text(f'''include "{RUNTIME_ROOT}/conf/modules.default.conf";
include "{RUNTIME_ROOT}/conf/snomasks.default.conf";
blacklist-module "geoip_classic";
blacklist-module "geoip_mmdb";
blacklist-module "geoip_csv";

me {{
    name "{name}";
    info "UDB isolated one-node integration harness";
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
    propagator "{name}";
}}
''', encoding="ascii")


def bwrap_command(node, ircd, config, configtest=False):
    # Keep the installed daemon immutable while isolating its mutable runtime paths.
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

    def wait_for(self, predicate, description, start=0, timeout=10):
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


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def names(client):
    return client.request("NAMES #vault", lambda line: " 366 " in line, "end of NAMES")


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


def malformed_persisted_paths_rejected(log):
    text = log.read_text(errors="replace")
    return (text.count("Skipping malformed persisted record in block N") == 4 and
            "Skipping overlong persisted record" in text)


def wait_for_log(predicate, log, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate(log):
            return True
        time.sleep(0.05)
    return predicate(log)


def exercise(host, port):
    clients = []
    try:
        alice = IrcClient(host, port, "alice-setup")
        clients.append(alice)
        alice.request("NICK alice:secret", lambda line: " NICK :alice" in line,
                      "nick change to registered nick")
        alice.wait_for(lambda line: " MODE alice " in line and "+r" in line, "nick registration +r")
        whois = alice.request("WHOIS alice", lambda line: " 318 " in line, "end of WHOIS")
        require(any("alice.test" in line for line in whois),
                f"WHOIS for alice does not contain UDB vhost: {whois!r}")

        alice.request("CAP REQ :multi-prefix", lambda line: " CAP " in line and
                      (" ACK " in line or " NAK " in line), "CAP reply")
        alice.request("JOIN #vault chansecret", lambda line: " 366 " in line, "end of founder JOIN")
        founder_names = names(alice)
        require(any("~alice" in line for line in founder_names),
                f"founder did not receive +q: {founder_names!r}")
        require(not any("@alice" in line for line in founder_names),
                f"founder received +o in addition to +q: {founder_names!r}")

        bob = IrcClient(host, port, "bob")
        clients.append(bob)
        rejected_nick = bob.request("NICK alice:wrong",
                                    lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                                    "rejection of invalid nick password")
        require(any("registered" in line.lower() or "password" in line.lower() for line in rejected_nick),
                f"invalid nick credentials not rejected by UDB: {rejected_nick!r}")

        # A profile without pass governs nick use only. Its effect records are
        # dormant, and access is a use restriction rather than authentication.
        bob.send("MODE bob +i")
        time.sleep(0.1)
        before_open_modes = bob.request("MODE bob", lambda line: " 221 " in line,
                                        "external +i confirmation")
        require(any("+i" in line for line in before_open_modes),
                f"external +i was not established before passless profile: {before_open_modes!r}")
        bob.request("NICK open", lambda line: " NICK :open" in line, "open unprotected profile")
        open_modes = bob.request("MODE open", lambda line: " 221 " in line, "open profile modes")
        require(any("+i" in line for line in open_modes) and not any("+r" in line for line in open_modes),
                f"passless profile removed external +i or granted UDB identity: {open_modes!r}")
        open_whois = bob.request("WHOIS open", lambda line: " 318 " in line, "open profile WHOIS")
        require(not any("open.test" in line or "Open profile" in line for line in open_whois),
                f"passless profile applied UDB effects: {open_whois!r}")

        # `!Password` must not turn arbitrary text into a ghost/recovery proof
        # when the profile has no credential.
        denied_recovery = alice.request("NICK open!whatever",
                                        lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                                        "passless nick recovery rejection")
        require(not any("Ghosting open" in line for line in denied_recovery),
                f"passless nick accepted privileged recovery: {denied_recovery!r}")
        bob.wait_for(lambda line: " NICK :open" in line, "passless nick holder remains connected")

        bob.request("NICK limited", lambda line: " NICK :limited" in line, "access-authorized passless profile")
        limited_modes = bob.request("MODE limited", lambda line: " 221 " in line, "limited profile modes")
        require(not any("+r" in line for line in limited_modes),
                f"access authorized a passless identity: {limited_modes!r}")
        limited_whois = bob.request("WHOIS limited", lambda line: " 318 " in line, "limited profile WHOIS")
        require(not any("limited.test" in line for line in limited_whois),
                f"access-authorized passless profile applied vhost: {limited_whois!r}")

        blocked = bob.request("NICK blocked", lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                              "access-denied passless profile")
        require(any("access" in line.lower() or "ip address" in line.lower() for line in blocked) and
                not any("requires a password" in line.lower() for line in blocked),
                f"passless access denial used password wording: {blocked!r}")

        suspended_client = IrcClient(host, port, "suspended-client")
        clients.append(suspended_client)
        suspended_client.request("NICK openhold", lambda line: " NICK :openhold" in line,
                                 "suspended passless profile")
        suspended_client.wait_for(lambda line: "This nickname is suspended. Reason: open maintenance" in line,
                                  "passless suspension reason")
        openhold_modes = suspended_client.request("MODE openhold", lambda line: " 221 ", "passless suspended modes")
        require(not any("+r" in line for line in openhold_modes),
                f"passless suspended profile granted UDB identity: {openhold_modes!r}")
        openhold_whois = suspended_client.request("WHOIS openhold", lambda line: " 318 " in line,
                                                  "passless suspended WHOIS")
        require(not any("openhold.test" in line for line in openhold_whois),
                f"passless suspended profile applied vhost: {openhold_whois!r}")

        # The NICK override owns the normal forbid path: it gives one reason
        # NOTICE and does not delegate to the hook that would create a 432.
        start = len(bob.lines)
        bob.send("NICK reserved:anything")
        bob.wait_for(lambda line: "This nickname is forbidden. Reason: reserved for testing" in line,
                     "forbid reason NOTICE", start=start)
        bob.receive(time.monotonic() + 0.5)
        forbid_lines = bob.lines[start:]
        require(sum("This nickname is forbidden. Reason: reserved for testing" in line for line in forbid_lines) == 1 and
                not any(" 432 " in line for line in forbid_lines),
                f"forbid produced a duplicate notice or 432: {forbid_lines!r}")

        # A suspended profile still requires its password. Once authenticated it
        # keeps the nick but deliberately has no UDB identity/effects.
        for command in ("NICK suspended", "NICK suspended:wrong"):
            rejected_suspend = suspended_client.request(command,
                                                        lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                                                        "rejection of unauthenticated suspended nick")
            require(any("password" in line.lower() or "registered" in line.lower()
                        for line in rejected_suspend),
                    f"suspended nick bypassed password validation: {rejected_suspend!r}")
        suspended_client.request("NICK suspended:holdsecret", lambda line: " NICK :suspended" in line,
                                 "authenticated suspended nick")
        suspended_client.wait_for(lambda line: "This nickname is suspended. Reason: pending review" in line,
                                  "suspension reason")
        mode_lines = suspended_client.request("MODE suspended", lambda line: " 221 " in line,
                                              "suspended user mode reply")
        require(not any("+r" in line for line in mode_lines),
                f"suspended nick received UDB identity: {mode_lines!r}")
        require(not any("S" in line.split(" :", 1)[-1] for line in mode_lines),
                f"suspended nick implicitly received +S: {mode_lines!r}")
        suspended_whois = suspended_client.request("WHOIS suspended", lambda line: " 318 " in line,
                                                   "suspended WHOIS")
        require(not any("suspended.test" in line for line in suspended_whois),
                f"suspended nick received its UDB vhost: {suspended_whois!r}")

        for command in ("JOIN #firstkey", "JOIN #firstkey wrong"):
            rejected_join = suspended_client.request(command, lambda line: " 475 " in line,
                                                     "rejection of first JOIN with invalid channel key")
            require(any(" 475 " in line for line in rejected_join),
                    f"first JOIN key was not rejected: {rejected_join!r}")
        suspended_client.request("JOIN #firstkey firstsecret", lambda line: " 366 " in line, "first keyed JOIN")
        suspended_client.request("JOIN #vault chansecret", lambda line: " 366 " in line, "end of keyed JOIN")

        alice.request("NICK alice2", lambda line: " NICK :alice2" in line, "nick change")
        mode_reply = alice.wait_for(lambda line: " MODE alice2 " in line and "-r" in line,
                                    "removal of +r on nick drop")
        require(any("-r" in line for line in mode_reply),
                f"+r persisted after nick drop: {mode_reply!r}")
        require(any("-t" in line or "-rt" in line for line in mode_reply),
                f"UDB vhost persisted after nick drop: {mode_reply!r}")
        print("PASS: nick sha256 +r/vhost, founder-only +q, and native +k including the first JOIN")
    finally:
        for client in clients:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD,
                        help="UnrealIRCd binary (default: UDB_TEST_IRCD_ROOT/bin/unrealircd)")
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path(),
                        help="compiled UDB module")
    parser.add_argument("--timeout", type=int, default=15, help="daemon readiness timeout in seconds")
    parser.add_argument("--keep", action="store_true", help="preserve the temporary node directory")
    args = parser.parse_args()

    if not shutil.which("bwrap"):
        return skip("bwrap is required for the isolated PERMDATADIR mount namespace")
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip(f"installed modules.default.conf is unavailable under {RUNTIME_ROOT}")

    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-one-node-"))
    process = None
    try:
        node = root / "node"
        data = node / "runtime-data"
        data.mkdir(parents=True)
        (node / "tmp").mkdir()
        third_modules = node / "modules" / "third"
        third_modules.mkdir(parents=True)
        shutil.copy2(args.module, third_modules / "udb.so")
        seed_block(data / "udb_N.db", "N",
                   f"alice::pass sha256:{sha256('secret')}\n"
                   "alice::access 127.0.0.0/8\n"
                   "alice::vhost alice.test\n"
                   f"reserved::forbid reserved for testing\n"
                   "open::vhost open.test\n"
                   "open::modes +i\n"
                   "open::swhois Open profile\n"
                   "limited::access 127.0.0.0/8\n"
                   "limited::vhost limited.test\n"
                   "blocked::access 192.0.2.0/24\n"
                   "openhold::access 127.0.0.0/8\n"
                   "openhold::suspend open maintenance\n"
                   "openhold::vhost openhold.test\n"
                   f"suspended::pass sha256:{sha256('holdsecret')}\n"
                   "suspended::access 127.0.0.0/8\n"
                   "suspended::suspend pending review\n"
                   "suspended::vhost suspended.test\n")
        seed_block(data / "udb_C.db", "C",
                   "#vault::founder alice\n"
                   "#vault::modes +ntk chansecret\n"
                   "#firstkey::modes +ntk firstsecret\n")
        seed_block(data / "udb_L.db", "L",
                   "udb-one.test::options *1\n")
        for block in ("I", "S", "K"):
            seed_block(data / f"udb_{block}.db", block)
        seed_ready_state(data)
        port, tls_port = free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "udb-one.test", "0A1", port, tls_port, args.module, data)
        run_configtest(node, args.ircd, config)
        log = node / "ircd.log"
        with log.open("w") as output:
            process = subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=output,
                                        stderr=subprocess.STDOUT, text=True)
        wait_for_daemon(process, "127.0.0.1", port, args.timeout)
        exercise("127.0.0.1", port)
        require(wait_for_log(lambda path: "No connected ULine user matches" in
                             path.read_text(errors="replace"), log, args.timeout),
                "missing service clients did not produce a safe UDB source fallback log")
        print("PASS: missing service clients used the local server source and logged the fallback")
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
        if args.keep:
            print(f"Temporary files retained at: {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
