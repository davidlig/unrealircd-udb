#!/usr/bin/env python3
"""Runtime contract for active UDB identity, suspend revocation and one-shot nick auth.

Semantics under test:

  authentication -> active UDB identity + effects
  INS suspend    -> revoke identity/effects, keep the nick
  DEL suspend    -> Guest rename for protected profiles (reauth required)
  SVSNICK        -> never authentication: protected nicks are renamed away
"""

import argparse
import hashlib
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zlib

from runtime_schema_validation import (
    DEFAULT_IRCD,
    EnvironmentUnavailable,
    FakeServicesServer,
    IRCD_SID,
    IrcClient as BaseIrcClient,
    REPO_ROOT,
    RUNTIME_ROOT,
    bwrap_command,
    find_module_path,
    free_port,
    run_configtest,
    skip,
    stop,
    write_config,
)


# Fake services can exempt test clients from UnrealIRCd fake lag; without it a
# long script of NICK commands accumulates parsing lag and stalls responses.
_services = None

STATE_SOURCE = pathlib.Path(__file__).resolve().parent / "udb_test_state.c"
STATE_MODULE = STATE_SOURCE.with_suffix(".so")


def build_state_module():
    if STATE_MODULE.is_file() and STATE_MODULE.stat().st_mtime >= STATE_SOURCE.stat().st_mtime:
        return STATE_MODULE
    src_root = pathlib.Path(os.environ.get("UNREALIRCD_SRC_ROOT",
                                           REPO_ROOT.parents[3] if len(REPO_ROOT.parents) > 3 and
                                           (REPO_ROOT.parents[3] / "Makefile").is_file() else REPO_ROOT))
    result = subprocess.run(["make", "custommodule", "MODULEFILE=udb/tests/udb_test_state"], cwd=src_root,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if result.returncode or not STATE_MODULE.is_file():
        raise RuntimeError(f"test state module build failed:\n{result.stdout}")
    print(f"PASS: built test-only state fixture {STATE_MODULE.name}")
    return STATE_MODULE


class IrcClient(BaseIrcClient):
    def __init__(self, host, port, nick):
        super().__init__(host, port, nick)
        if _services is not None:
            _services.send(f"SVSNOLAG + {self.nick}")

    def request(self, command, terminator, description, timeout=20):
        start = len(self.lines)
        self.send(command)
        return self.wait_for(terminator, description, start=start, timeout=timeout)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(value):
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def wait_for_mode(client, nick, required, description, start=0, timeout=15):
    lines = client.wait_for(lambda line: f" MODE {nick} " in line and required in line,
                            description, start=start, timeout=timeout)
    require(any(required in line for line in lines), f"{description}: {lines!r}")


def request(client, command, terminator, description, timeout=20):
    start = len(client.lines)
    client.send(command)
    return client.wait_for(terminator, description, start=start, timeout=timeout)


def current_nick(client):
    nick = client.nick
    for line in client.lines:
        if " NICK :" in line:
            nick = line.split(" NICK :", 1)[1].split(" ", 1)[0]
    return nick


def has_usermode(client, nick, mode, timeout=15):
    start = len(client.lines)
    client.send(f"MODE {nick}")
    lines = client.wait_for(lambda line: " 221 " in line, f"{nick} modes", start=start, timeout=timeout)
    mode_strings = [line.rsplit(" ", 1)[-1].lstrip(":") for line in lines if " 221 " in line]
    return any(mode in value for value in mode_strings)


def assert_unidentified(client, nick, description, timeout=15):
    start = len(client.lines)
    client.send(f"MODE {nick}")
    modes = client.wait_for(lambda line: " 221 " in line, description, start=start, timeout=timeout)
    require(not any("+r" in line for line in modes), f"{description}: +r leaked: {modes!r}")
    start = len(client.lines)
    client.send(f"WHOIS {nick}")
    whois = client.wait_for(lambda line: " 318 " in line, description + " WHOIS", start=start, timeout=timeout)
    require(not any(f"{nick}.test" in line for line in whois), f"{description}: vhost leaked: {whois!r}")


class RegistrationClient(IrcClient):
    """Initial registration whose first NICK carries the one-shot credential."""

    def __init__(self, host, port, nick, password=None, user_first=False):
        self.nick = nick
        self.sock = socket.create_connection((host, port), timeout=3)
        self.sock.settimeout(0.25)
        self.lines = []
        self.buffer = ""
        credential = f"{nick}:{password}" if password else nick
        if user_first:
            self.send(f"USER {nick} 0 * :{nick}")
            self.send(f"NICK {credential}")
        else:
            self.send(f"NICK {credential}")
            self.send(f"USER {nick} 0 * :{nick}")
        self.wait_for(lambda line: " 001 " in line, "welcome", timeout=20)
        if _services is not None:
            _services.send(f"SVSNOLAG + {self.nick}")


def tree_checksum(records):
    lines = sorted(f"{path} {value}\n".encode("ascii") for path, value in records)
    return f"{zlib.crc32(b''.join(lines)) & 0xFFFFFFFF:08X}"


def replace_n_tree(services, round_id, txid, records):
    checksum = tree_checksum(records)
    start = len(services.lines)
    services.send(f"DB {services.ircd_sid} INF {round_id} N {checksum} {int(time.time()) + 1000 + round_id}")
    services.wait_for(lambda line: f" RES {round_id} N" in line,
                      f"N snapshot request for {txid}", start=start)
    services.send(f"DB {services.ircd_sid} BEGIN {round_id} N {txid} 00000000")
    for path, value in records:
        services.send(f"DB {services.ircd_sid} PUT {round_id} N {txid} {path} :{value}")
    services.send(f"DB {services.ircd_sid} END {round_id} N {txid} {checksum}")


def wait_for_db_records(path, present, absent=(), timeout=5):
    deadline = time.monotonic() + timeout
    content = ""
    while time.monotonic() < deadline:
        content = path.read_text(encoding="ascii") if path.exists() else ""
        if all(record in content for record in present) and all(record not in content for record in absent):
            return content
        time.sleep(0.05)
    raise AssertionError(f"snapshot did not persist expected N records: {content!r}")


def add_profile(services, nick, password, suspended=True):
    services.send_ins(f"N::{nick}::pass", "sha256:" + sha256(password))
    services.send_ins(f"N::{nick}::access", "127.0.0.0/8")
    services.send_ins(f"N::{nick}::vhost", f"{nick}.test")
    if suspended:
        services.send_ins(f"N::{nick}::suspend", "manual review")
    time.sleep(0.25)


def adopt_suspended(host, port, nick, password):
    client = IrcClient(host, port, nick + "-client")
    request(client, f"NICK {nick}:{password}", lambda line: f" NICK :{nick}" in line,
            "authenticated suspended nick", timeout=20)
    client.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "suspension reason", timeout=20)
    assert_unidentified(client, nick, "suspended authenticated client")
    return client


def free_guest(client, description, start=0, timeout=20):
    client.wait_for(lambda line: " NICK :Guest" in line, description, start=start, timeout=timeout)
    return current_nick(client)


def add_effect_profile(services, nick, password, effects=(), suspended=False):
    services.send_ins(f"N::{nick}::pass", "sha256:" + sha256(password))
    services.send_ins(f"N::{nick}::access", "127.0.0.0/8")
    for path, value in effects:
        services.send_ins(f"N::{nick}::{path}", value)
    if suspended:
        services.send_ins(f"N::{nick}::suspend", "manual review")
    time.sleep(0.25)


def set_external_snomask(services, nick, mask):
    services.send(f"UDBTEST SNOMASK {nick} {mask}")


def read_snomask(services, client, nick, description, timeout=10):
    start = len(client.lines)
    services.send(f"UDBTEST GETSNOMASK {nick}")
    client.wait_for(lambda line: "UDBTEST SNOMASK=" in line, description, start=start, timeout=timeout)
    for line in reversed(client.lines[start:]):
        if "UDBTEST SNOMASK=" in line:
            value = line.split("UDBTEST SNOMASK=", 1)[1].strip()
            return "" if value == "-" else value
    return ""


def read_oper_state(services, client, nick, description="get oper state", timeout=10):
    start = len(client.lines)
    services.send(f"UDBTEST GETOPER {nick}")
    client.wait_for(lambda line: "UDBTEST OPER=" in line, description, start=start, timeout=timeout)
    for line in reversed(client.lines[start:]):
        if "UDBTEST OPER=" in line:
            parts = line.split("UDBTEST ", 1)[1].strip().split()
            data = {}
            for part in parts:
                if "=" in part:
                    k, v = part.split("=", 1)
                    data[k] = v
            return {
                "oper": data.get("OPER") == "1",
                "operclass": data.get("OPERCLASS"),
                "opercount": int(data.get("OPERCOUNT", 0)),
                "operlist": int(data.get("OPERLIST", 0)),
            }
    raise AssertionError(f"Could not parse oper state from lines: {client.lines[start:]!r}")


def authenticate(client, nick, password, description="authentication", timeout=20):
    client.request(f"NICK {nick}:{password}", lambda line: f" NICK :{nick}" in line, description,
                   timeout=timeout)
    wait_for_mode(client, nick, "+r", description + " +r", timeout=15)


def run_tests(ircd, module, keep=False):
    global _services
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-nick-auth-"))
    process = None
    stdout_handle = None
    services = None
    clients = []
    try:
        node = root / "node"
        for path in (node / "runtime-data", node / "tmp", node / "modules" / "third"):
            path.mkdir(parents=True, exist_ok=True)
        shutil.copy2(module, node / "modules" / "third" / "udb.so")
        shutil.copy2(build_state_module(), node / "modules" / "third" / "udb_test_state.so")
        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "ircd.test", IRCD_SID, client_port, server_port, tls_port,
                     node / "modules" / "third" / "udb.so", node / "runtime-data",
                     extra_modules=("third/udb_test_state",))
        with config.open("a", encoding="ascii") as handle:
            handle.write(f'include "{RUNTIME_ROOT}/conf/operclass.default.conf";\n')
            handle.write('oper testoper {\n'
                         '    mask *;\n'
                         '    password "operpass";\n'
                         '    operclass locop;\n'
                         '    class clients;\n'
                         '};\n')
            handle.write('oper netadminoper {\n'
                         '    mask *;\n'
                         '    password "netadminpass";\n'
                         '    operclass netadmin;\n'
                         '    class clients;\n'
                         '};\n')
            handle.write("set { anti-flood { known-users { nick-flood 20:60; } "
                         "unknown-users { nick-flood 20:60; } } }\n")
        run_configtest(node, ircd, config)
        stdout_path = node / "logs" / "ircd.stdout"
        stdout_handle = stdout_path.open("w", encoding="utf-8")
        process = subprocess.Popen(bwrap_command(node, ircd, config), stdout=stdout_handle,
                                   stderr=subprocess.STDOUT, text=True)
        time.sleep(1)
        if process.poll() is not None:
            stdout_handle.close()
            with stdout_path.open("r", encoding="utf-8", errors="replace") as h:
                output = h.read()
            raise RuntimeError(f"ircd exited immediately:\n{output}")
        services = FakeServicesServer("127.0.0.1", server_port)
        _services = services

        # A: authentication creates active identity and effects.
        add_profile(services, "owner", "ownersecret", suspended=False)
        owner = IrcClient("127.0.0.1", client_port, "owner-client")
        clients.append(owner)
        owner.request("NICK owner:ownersecret", lambda line: " NICK :owner" in line, "owner identification")
        wait_for_mode(owner, "owner", "+r", "owner +r")
        whois = owner.request("WHOIS owner", lambda line: " 318 " in line, "owner WHOIS")
        require(any("owner.test" in line for line in whois), f"authentication did not apply vhost: {whois!r}")

        # B: INS suspend revokes identity/effects and keeps the nick.
        start = len(owner.lines)
        services.send_ins("N::owner::suspend", "manual review")
        owner.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "hot suspend notice", start=start)
        require(current_nick(owner) == "owner", "hot suspend renamed the holder")
        assert_unidentified(owner, "owner", "hot suspended owner")

        # C: DEL suspend does not restore anything: protected profiles are renamed.
        start = len(owner.lines)
        services.send_del("N::owner::suspend")
        guest = free_guest(owner, "unsuspend rename", start=start)
        require(guest != "owner", "DEL suspend kept the unauthenticated holder on the nick")
        assert_unidentified(owner, guest, "unsuspended holder")

        # D: explicit re-authentication recovers the profile from scratch.
        start = len(owner.lines)
        owner.request("NICK owner:ownersecret", lambda line: " NICK :owner" in line, "owner reauthentication")
        wait_for_mode(owner, "owner", "+r", "owner reauth +r", start=start, timeout=15)
        whois = request(owner, "WHOIS owner", lambda line: " 318 " in line, "owner reauth WHOIS", timeout=20)
        require(any("owner.test" in line for line in whois), f"reauth did not restore vhost: {whois!r}")

        # E: suspended adoption validates the credential, keeps no identity, and
        # DEL suspend renames the holder. A wrong password is denied.
        add_profile(services, "held", "heldsecret")
        held = adopt_suspended("127.0.0.1", client_port, "held", "heldsecret")
        clients.append(held)
        wrongpass = IrcClient("127.0.0.1", client_port, "wrongpass")
        clients.append(wrongpass)
        denied = wrongpass.request("NICK held:wrong", lambda line: any(code in line for code in (" 432 ", " 433 ", " 437 ")),
                                   "suspended wrong password rejection")
        require(any("password" in line.lower() for line in denied), f"wrong password not denied: {denied!r}")
        start = len(held.lines)
        services.send_del("N::held::suspend")
        free_guest(held, "suspended DEL rename", start=start)
        assert_unidentified(held, current_nick(held), "suspended DEL holder")

        # E2: adopting a suspended profile materializes nothing, so externally
        # supplied state (here +S) must survive even when the profile lists it.
        services.send_ins("N::helds::pass", "sha256:" + sha256("heldssecret"))
        services.send_ins("N::helds::access", "127.0.0.0/8")
        services.send_ins("N::helds::modes", "+S")
        services.send_ins("N::helds::suspend", "manual review")
        time.sleep(0.25)
        helds = IrcClient("127.0.0.1", client_port, "helds-external")
        clients.append(helds)
        services.send("SVS2MODE helds-external +S")
        wait_for_mode(helds, "helds-external", "+S", "external +S before suspended adoption")
        helds.request("NICK helds:heldssecret", lambda line: " NICK :helds" in line, "suspended +S adoption")
        helds.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "suspended +S notice", timeout=15)
        helds_modes = request(helds, "MODE helds", lambda line: " 221 " in line, "suspended +S modes", timeout=15)
        require(not any("+r" in line for line in helds_modes),
                f"suspended adoption granted UDB identity: {helds_modes!r}")
        require(any("S" in line.split(" :", 1)[-1] for line in helds_modes),
                f"suspended adoption stripped external +S: {helds_modes!r}")

        # E3: same for an externally owned vhost: adoption must not apply the
        # dormant UDB vhost nor remove the external one.
        services.send_ins("N::heldv::pass", "sha256:" + sha256("heldvsecret"))
        services.send_ins("N::heldv::access", "127.0.0.0/8")
        services.send_ins("N::heldv::vhost", "udb-held.test")
        services.send_ins("N::heldv::suspend", "manual review")
        time.sleep(0.25)
        heldv = IrcClient("127.0.0.1", client_port, "heldv-external")
        clients.append(heldv)
        services.send("CHGHOST heldv-external external-held.test")
        heldv.wait_for(lambda line: " 396 " in line and "external-held.test" in line,
                       "external vhost before suspended adoption", timeout=15)
        heldv.request("NICK heldv:heldvsecret", lambda line: " NICK :heldv" in line, "suspended vhost adoption")
        heldv.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "suspended vhost notice", timeout=15)
        heldv_whois = request(heldv, "WHOIS heldv", lambda line: " 318 " in line,
                              "suspended vhost WHOIS", timeout=15)
        require(any("external-held.test" in line for line in heldv_whois) and
                not any("udb-held.test" in line for line in heldv_whois),
                f"suspended adoption replaced external vhost: {heldv_whois!r}")

        # F: a passless suspended profile has no identity to revoke; DEL suspend
        # keeps the nick while access allows it.
        services.send_ins("N::openhold::access", "127.0.0.0/8")
        services.send_ins("N::openhold::suspend", "manual review")
        time.sleep(0.25)
        openhold = IrcClient("127.0.0.1", client_port, "openhold-client")
        clients.append(openhold)
        openhold.request("NICK openhold", lambda line: " NICK :openhold" in line, "passless suspended nick")
        openhold.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                          "passless suspension reason")
        assert_unidentified(openhold, "openhold", "passless suspended profile")
        start = len(openhold.lines)
        services.send_del("N::openhold::suspend")
        openhold.receive(time.monotonic() + 0.6)
        require(current_nick(openhold) == "openhold", "passless DEL suspend renamed the holder")
        assert_unidentified(openhold, "openhold", "passless unsuspended profile")

        # G: passless effects stay inert: dormant modes/vhosts never override or
        # remove externally owned state.
        services.send_ins("N::opens::access", "127.0.0.0/8")
        services.send_ins("N::opens::modes", "+S")
        services.send_ins("N::opens::vhost", "opens.test")
        time.sleep(0.2)
        external = IrcClient("127.0.0.1", client_port, "external-mode")
        clients.append(external)
        services.send("SVS2MODE external-mode +S")
        wait_for_mode(external, "external-mode", "+S", "external +S assignment")
        services.send("CHGHOST external-mode external-owned.test")
        external.wait_for(lambda line: " 396 " in line and "external-owned.test" in line,
                          "external vhost assignment", timeout=10)
        external.request("NICK opens", lambda line: " NICK :opens" in line, "passless +S profile")
        opens_modes = external.request("MODE opens", lambda line: " 221 " in line, "passless +S modes")
        require(any("S" in line.split(" :", 1)[-1] for line in opens_modes),
                f"passless profile removed externally assigned +S: {opens_modes!r}")
        opens_whois = external.request("WHOIS opens", lambda line: " 318 " in line, "passless external vhost")
        require(any("external-owned.test" in line for line in opens_whois) and
                not any("opens.test" in line for line in opens_whois),
                f"passless profile replaced or removed external vhost: {opens_whois!r}")

        # H: a hot effect update keeps active identity and refreshes effects.
        add_profile(services, "hotctrl", "hotctrlsecret", suspended=False)
        hotctrl = IrcClient("127.0.0.1", client_port, "hotctrl-client")
        clients.append(hotctrl)
        hotctrl.request("NICK hotctrl:hotctrlsecret", lambda line: " NICK :hotctrl" in line,
                        "hot control identification")
        wait_for_mode(hotctrl, "hotctrl", "+r", "hot control +r", timeout=10)
        services.send_ins("N::hotctrl::vhost", "hotctrl-updated.test")
        time.sleep(0.4)
        require(has_usermode(hotctrl, "hotctrl", "r", timeout=15), "hot effect update lost identity")
        hotctrl_whois = request(hotctrl, "WHOIS hotctrl", lambda line: " 318 " in line,
                                "hot control vhost", timeout=15)
        require(any("hotctrl-updated.test" in line for line in hotctrl_whois),
                f"hot effect update did not refresh effects: {hotctrl_whois!r}")

        # I: changing pass or access invalidates identity; UDB strips owned
        # effects and renames even when the new access CIDR still permits.
        add_profile(services, "passchg", "oldsecret", suspended=False)
        passchg = IrcClient("127.0.0.1", client_port, "passchg-client")
        clients.append(passchg)
        passchg.request("NICK passchg:oldsecret", lambda line: " NICK :passchg" in line, "pass change source")
        wait_for_mode(passchg, "passchg", "+r", "pass change +r")
        start = len(passchg.lines)
        services.send_ins("N::passchg::pass", "sha256:" + sha256("newsecret"))
        free_guest(passchg, "pass change rename", start=start)
        assert_unidentified(passchg, current_nick(passchg), "pass changed holder")

        add_profile(services, "acctchg", "acctsecret", suspended=False)
        acctchg = IrcClient("127.0.0.1", client_port, "acctchg-client")
        clients.append(acctchg)
        acctchg.request("NICK acctchg:acctsecret", lambda line: " NICK :acctchg" in line, "access change source")
        wait_for_mode(acctchg, "acctchg", "+r", "access change +r")
        start = len(acctchg.lines)
        services.send_ins("N::acctchg::access", "127.0.0.0/9")
        free_guest(acctchg, "access change rename", start=start)
        assert_unidentified(acctchg, current_nick(acctchg), "access changed holder")

        # I2: deleting the access record goes through the same candidate
        # reapply and revokes identity.
        add_profile(services, "acctdel", "acctdelsecret", suspended=False)
        acctdel = IrcClient("127.0.0.1", client_port, "acctdel-client")
        clients.append(acctdel)
        acctdel.request("NICK acctdel:acctdelsecret", lambda line: " NICK :acctdel" in line,
                        "access delete source")
        wait_for_mode(acctdel, "acctdel", "+r", "access delete +r")
        start = len(acctdel.lines)
        services.send_del("N::acctdel::access")
        free_guest(acctdel, "access delete rename", start=start)
        assert_unidentified(acctdel, current_nick(acctdel), "access deleted holder")

        # I3: deleting a non-policy effect keeps identity and only removes that
        # effect; the remaining profile still validates against the digest.
        add_profile(services, "effectdel", "effectdelsecret", suspended=False)
        effectdel = IrcClient("127.0.0.1", client_port, "effectdel-client")
        clients.append(effectdel)
        effectdel.request("NICK effectdel:effectdelsecret", lambda line: " NICK :effectdel" in line,
                          "effect delete source")
        wait_for_mode(effectdel, "effectdel", "+r", "effect delete +r")
        services.send_del("N::effectdel::vhost")
        time.sleep(0.4)
        effectdel.receive(time.monotonic() + 0.3)
        require(has_usermode(effectdel, "effectdel", "r", timeout=15),
                "deleting a non-policy effect revoked identity")
        effect_whois = request(effectdel, "WHOIS effectdel", lambda line: " 318 " in line,
                               "effect delete WHOIS", timeout=15)
        require(not any("effectdel.test" in line for line in effect_whois),
                f"deleted vhost was not removed: {effect_whois!r}")

        # J: passless -> pass never auto-identifies its holder; after explicit
        # auth, DEL pass revokes identity but keeps the nick under access.
        services.send_ins("N::hotpass::access", "127.0.0.0/8")
        services.send_ins("N::hotpass::vhost", "hotpass.test")
        time.sleep(0.2)
        hotpass = IrcClient("127.0.0.1", client_port, "hotpass-client")
        clients.append(hotpass)
        hotpass.request("NICK hotpass", lambda line: " NICK :hotpass" in line, "passless hotpass nick")
        assert_unidentified(hotpass, "hotpass", "initial passless hotpass profile")
        start = len(hotpass.lines)
        services.send_ins("N::hotpass::pass", "sha256:" + sha256("hotsecret"))
        free_guest(hotpass, "passless to pass rename", start=start)
        hotpass.request("NICK hotpass:hotsecret", lambda line: " NICK :hotpass" in line,
                        "explicit hotpass authentication")
        wait_for_mode(hotpass, "hotpass", "+r", "hotpass explicit +r", timeout=10)
        start = len(hotpass.lines)
        services.send_del("N::hotpass::pass")
        wait_for_mode(hotpass, "hotpass", "-r", "hot pass deletion", start=start)
        require(current_nick(hotpass) == "hotpass", "DEL pass renamed the holder")
        assert_unidentified(hotpass, "hotpass", "DEL pass holder")

        # K: external account/+r is never UDB identity; the passless->pass
        # transition renames the holder, and explicit auth recovers it.
        services.send_ins("N::extacct::access", "127.0.0.0/8")
        services.send_ins("N::extacct::vhost", "extacct.udb")
        time.sleep(0.2)
        extacct = IrcClient("127.0.0.1", client_port, "extacct")
        clients.append(extacct)
        services.send("SVSLOGIN * extacct extacct")
        services.send("SVS2MODE extacct +r")
        wait_for_mode(extacct, "extacct", "+r", "external account identity")
        ext_login = request(extacct, "WHOIS extacct", lambda line: " 318 " in line,
                            "external account state", timeout=15)
        require(any(" 330 " in line and "extacct" in line for line in ext_login),
                f"external account was not established: {ext_login!r}")
        start = len(extacct.lines)
        services.send_ins("N::extacct::pass", "sha256:" + sha256("extacctsecret"))
        guest = free_guest(extacct, "external account rename", start=start)
        ext_whois = extacct.request(f"WHOIS {guest}", lambda line: " 318 " in line, "external-account WHOIS")
        require(not any("extacct.udb" in line for line in ext_whois),
                f"INS pass applied UDB identity from external account: {ext_whois!r}")
        extacct.request("NICK extacct:extacctsecret", lambda line: " NICK :extacct" in line,
                        "explicit extacct authentication")
        wait_for_mode(extacct, "extacct", "+r", "explicit extacct +r", timeout=10)
        ext_whois = extacct.request("WHOIS extacct", lambda line: " 318 " in line, "explicit extacct vhost")
        require(any("extacct.udb" in line for line in ext_whois),
                f"explicit authentication did not apply protected profile: {ext_whois!r}")

        for client in clients:
            client.close()
        clients.clear()
        time.sleep(0.4)

        n_db = node / "runtime-data" / "udb_N.db"

        # L: an equivalent snapshot policy keeps active identity and refreshes
        # effects without requiring reauthentication.
        add_profile(services, "snapowner", "snapownersecret", suspended=False)
        snapowner = IrcClient("127.0.0.1", client_port, "snapowner-client")
        clients.append(snapowner)
        snapowner.request("NICK snapowner:snapownersecret", lambda line: " NICK :snapowner" in line,
                          "snapshot owner identification")
        wait_for_mode(snapowner, "snapowner", "+r", "snapshot owner +r")
        snap_a = [("snapowner::pass", "sha256:" + sha256("snapownersecret")),
                  ("snapowner::access", "127.0.0.0/8"),
                  ("snapowner::vhost", "snapowner-replaced.test")]
        replace_n_tree(services, 101, "nick-equivalent-a", snap_a)
        wait_for_db_records(n_db, ("snapowner::vhost snapowner-replaced.test",))
        time.sleep(0.3)
        require(has_usermode(snapowner, "snapowner", "r", timeout=15),
                "equivalent snapshot revoked identity")
        snap_whois = request(snapowner, "WHOIS snapowner", lambda line: " 318 " in line,
                             "snapshot equivalent WHOIS", timeout=15)
        require(any("snapowner-replaced.test" in line for line in snap_whois),
                f"equivalent snapshot did not refresh effects: {snap_whois!r}")

        # M: snapshot normal -> suspend revokes identity and keeps the nick.
        snap_b = [("snapowner::pass", "sha256:" + sha256("snapownersecret")),
                  ("snapowner::access", "127.0.0.0/8"),
                  ("snapowner::vhost", "snapowner.test"),
                  ("snapowner::suspend", "manual review")]
        start = len(snapowner.lines)
        replace_n_tree(services, 102, "nick-normal-to-suspend-b", snap_b)
        snapowner.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                           "snapshot suspend notice", start=start, timeout=10)
        require(current_nick(snapowner) == "snapowner", "snapshot normal->suspend renamed the holder")
        assert_unidentified(snapowner, "snapowner", "snapshot suspended owner")

        # N: snapshot suspend -> normal renames; suspend -> suspend keeps.
        start = len(snapowner.lines)
        replace_n_tree(services, 103, "nick-suspend-to-normal-c", snap_a)
        free_guest(snapowner, "snapshot suspend to normal rename", start=start)
        assert_unidentified(snapowner, current_nick(snapowner), "snapshot unsuspended holder")

        # O: snapshot with a changed policy revokes identity and renames.
        add_profile(services, "snappol", "snappolsecret", suspended=False)
        snappol = IrcClient("127.0.0.1", client_port, "snappol-client")
        clients.append(snappol)
        snappol.request("NICK snappol:snappolsecret", lambda line: " NICK :snappol" in line,
                        "snapshot policy identification")
        wait_for_mode(snappol, "snappol", "+r", "snapshot policy +r")
        snap_c = [("snappol::pass", "sha256:" + sha256("changedsecret")),
                  ("snappol::access", "127.0.0.0/8")]
        start = len(snappol.lines)
        replace_n_tree(services, 104, "nick-policy-change-d", snap_c)
        free_guest(snappol, "snapshot policy change rename", start=start)
        assert_unidentified(snappol, current_nick(snappol), "snapshot policy changed holder")

        # P: a removed profile revokes identity/effects but does not rename; the
        # nick is simply no longer registered.
        add_profile(services, "snapgone", "snapgonesecret", suspended=False)
        snapgone = IrcClient("127.0.0.1", client_port, "snapgone-client")
        clients.append(snapgone)
        snapgone.request("NICK snapgone:snapgonesecret", lambda line: " NICK :snapgone" in line,
                         "snapshot removal identification")
        wait_for_mode(snapgone, "snapgone", "+r", "snapshot removal +r")
        start = len(snapgone.lines)
        replace_n_tree(services, 105, "nick-profile-removed-e", [("other::access", "127.0.0.0/8")])
        time.sleep(0.4)
        snapgone.receive(time.monotonic() + 0.3)
        require(current_nick(snapgone) == "snapgone", "removed profile forced a rename")
        assert_unidentified(snapgone, "snapgone", "removed profile holder")

        # Q: snapshot passless -> pass never manufactures identity.
        services.send_ins("N::snapx::access", "127.0.0.0/8")
        services.send_ins("N::snapx::vhost", "snapx.udb")
        time.sleep(0.25)
        snapx = IrcClient("127.0.0.1", client_port, "snapx")
        clients.append(snapx)
        services.send("SVSLOGIN * snapx snapx")
        services.send("SVS2MODE snapx +r")
        wait_for_mode(snapx, "snapx", "+r", "snapshot external account identity", timeout=10)
        snap_records = [("snapx::access", "127.0.0.0/8"),
                        ("snapx::vhost", "snapx.udb"),
                        ("snapx::pass", "sha256:" + sha256("snapxsecret"))]
        start = len(snapx.lines)
        replace_n_tree(services, 106, "nick-passless-protected-f", snap_records)
        wait_for_db_records(n_db, ("snapx::pass sha256:" + sha256("snapxsecret"),))
        guest = free_guest(snapx, "snapshot passless to pass rename", start=start)
        snapx_whois = snapx.request(f"WHOIS {guest}", lambda line: " 318 " in line, "snapshot external WHOIS")
        require(not any("snapx.udb" in line for line in snapx_whois),
                f"snapshot passless->pass applied UDB identity from external account: {snapx_whois!r}")

        # R: snapshot suspend -> suspend keeps the nick and never yields identity.
        add_profile(services, "snaphold", "snapholdsecret")
        snaphold = adopt_suspended("127.0.0.1", client_port, "snaphold", "snapholdsecret")
        clients.append(snaphold)
        snap_d = [("snaphold::pass", "sha256:" + sha256("snapholdsecret")),
                  ("snaphold::access", "127.0.0.0/8"),
                  ("snaphold::vhost", "snaphold-replaced.test"),
                  ("snaphold::suspend", "manual review")]
        start = len(snaphold.lines)
        replace_n_tree(services, 107, "nick-suspend-to-suspend-g", snap_d)
        wait_for_db_records(n_db, ("snaphold::vhost snaphold-replaced.test",))
        time.sleep(0.3)
        require(current_nick(snaphold) == "snaphold", "suspend->suspend snapshot renamed the holder")
        assert_unidentified(snaphold, "snaphold", "snapshot re-suspended holder")

        # R2: snapshot normal -> suspend strips only the old profile once. A new
        # candidate +S must not remove an externally owned +S.
        services.send_ins("N::snapmode::pass", "sha256:" + sha256("snapmodesecret"))
        services.send_ins("N::snapmode::access", "127.0.0.0/8")
        time.sleep(0.25)
        snapmode = IrcClient("127.0.0.1", client_port, "snapmode-client")
        clients.append(snapmode)
        snapmode.request("NICK snapmode:snapmodesecret", lambda line: " NICK :snapmode" in line,
                         "snapshot modes identification")
        wait_for_mode(snapmode, "snapmode", "+r", "snapshot modes +r")
        services.send("SVS2MODE snapmode +S")
        wait_for_mode(snapmode, "snapmode", "+S", "external +S before snapshot suspend")
        snap_e = [("snapmode::pass", "sha256:" + sha256("snapmodesecret")),
                  ("snapmode::access", "127.0.0.0/8"),
                  ("snapmode::modes", "+S"),
                  ("snapmode::suspend", "manual review")]
        start = len(snapmode.lines)
        replace_n_tree(services, 108, "nick-normal-suspend-modes", snap_e)
        snapmode.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                          "snapshot modes suspend notice", start=start, timeout=15)
        require(current_nick(snapmode) == "snapmode", "snapshot normal->suspend renamed the holder")
        require(not has_usermode(snapmode, "snapmode", "r", timeout=15),
                "snapshot normal->suspend kept identity")
        require(has_usermode(snapmode, "snapmode", "S", timeout=15),
                "candidate modes stripped externally owned +S")

        # R3: snapshot normal -> suspend with a new candidate vhost must not
        # remove an externally owned vhost nor apply the dormant one.
        services.send_ins("N::snapvhost::pass", "sha256:" + sha256("snapvhostsecret"))
        services.send_ins("N::snapvhost::access", "127.0.0.0/8")
        time.sleep(0.25)
        snapvhost = IrcClient("127.0.0.1", client_port, "snapvhost-client")
        clients.append(snapvhost)
        snapvhost.request("NICK snapvhost:snapvhostsecret", lambda line: " NICK :snapvhost" in line,
                          "snapshot vhost identification")
        wait_for_mode(snapvhost, "snapvhost", "+r", "snapshot vhost +r")
        services.send("CHGHOST snapvhost external-snapvhost.test")
        snapvhost.wait_for(lambda line: " 396 " in line and "external-snapvhost.test" in line,
                           "external vhost before snapshot suspend", timeout=15)
        snap_f = [("snapvhost::pass", "sha256:" + sha256("snapvhostsecret")),
                  ("snapvhost::access", "127.0.0.0/8"),
                  ("snapvhost::vhost", "udb-snapvhost.test"),
                  ("snapvhost::suspend", "manual review")]
        start = len(snapvhost.lines)
        replace_n_tree(services, 110, "nick-normal-suspend-vhost", snap_f)
        snapvhost.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                           "snapshot vhost suspend notice", start=start, timeout=15)
        require(current_nick(snapvhost) == "snapvhost", "snapshot vhost transition renamed the holder")
        snapvhost_whois = request(snapvhost, "WHOIS snapvhost", lambda line: " 318 " in line,
                                  "snapshot vhost WHOIS", timeout=15)
        require(any("external-snapvhost.test" in line for line in snapvhost_whois) and
                not any("udb-snapvhost.test" in line for line in snapvhost_whois),
                f"candidate vhost replaced external vhost: {snapvhost_whois!r}")

        # R4: an active profile without N::vhost must not strip an external
        # vhost on INS suspend.
        services.send_ins("N::novhost::pass", "sha256:" + sha256("novhostsecret"))
        services.send_ins("N::novhost::access", "127.0.0.0/8")
        time.sleep(0.25)
        novhost = IrcClient("127.0.0.1", client_port, "novhost-client")
        clients.append(novhost)
        novhost.request("NICK novhost:novhostsecret", lambda line: " NICK :novhost" in line,
                        "no-vhost profile identification")
        wait_for_mode(novhost, "novhost", "+r", "no-vhost profile +r")
        services.send("CHGHOST novhost external-novhost.test")
        novhost.wait_for(lambda line: " 396 " in line and "external-novhost.test" in line,
                         "external vhost before hot suspend", timeout=15)
        start = len(novhost.lines)
        services.send_ins("N::novhost::suspend", "manual review")
        novhost.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                         "no-vhost suspend notice", start=start, timeout=15)
        require(not has_usermode(novhost, "novhost", "r", timeout=15),
                "hot suspend kept identity on a no-vhost profile")
        novhost_whois = request(novhost, "WHOIS novhost", lambda line: " 318 " in line,
                                "no-vhost WHOIS", timeout=15)
        require(any("external-novhost.test" in line for line in novhost_whois),
                f"strip removed an external vhost without N::vhost: {novhost_whois!r}")

        for client in clients:
            client.close()
        clients.clear()
        time.sleep(0.4)

        # S: SVSNICK is never authentication. A protected target is renamed
        # away, with no identity and no effects.
        add_profile(services, "forced", "forcedsecret", suspended=False)
        forced_guest = IrcClient("127.0.0.1", client_port, "forced-guest")
        clients.append(forced_guest)
        services.send(f"SVSNICK {forced_guest.nick} forced {int(time.time())}")
        free_guest(forced_guest, "forced protected safe rename")
        forced_nick = current_nick(forced_guest)
        require(forced_nick != "forced", "UDB kept an unauthenticated client on a protected nick")
        require(not has_usermode(forced_guest, forced_nick, "r", timeout=15),
                "forced rename to a protected profile granted +r")
        forced_whois = request(forced_guest, f"WHOIS {forced_nick}", lambda line: " 318 " in line,
                               "forced protected WHOIS", timeout=15)
        require(not any("forced.test" in line for line in forced_whois),
                f"forced rename applied UDB effects: {forced_whois!r}")

        add_profile(services, "forcedheld", "forcedheldsecret")
        held_guest = IrcClient("127.0.0.1", client_port, "forcedheld-guest")
        clients.append(held_guest)
        services.send(f"SVSNICK {held_guest.nick} forcedheld {int(time.time())}")
        free_guest(held_guest, "forced suspended safe rename")
        held_nick = current_nick(held_guest)
        require(held_nick != "forcedheld", "UDB kept an unauthenticated client on a suspended nick")
        assert_unidentified(held_guest, held_nick, "forced suspended holder", timeout=15)
        hold_start = len(held_guest.lines)
        services.send_del("N::forcedheld::suspend")
        held_guest.receive(time.monotonic() + 0.8)
        hold_events = held_guest.lines[hold_start:]
        require(not any(" MODE " in line and "+r" in line for line in hold_events),
                f"forced suspended rename left a restorable identity: {hold_events!r}")

        # T: a credential is one-shot and belongs to its attempt: a stale
        # password never authenticates another profile nor consumes its flood
        # tracker, and a failed attempt followed by SVSNICK grants nothing.
        add_profile(services, "aliceoneshot", "sharedoneshot", suspended=False)
        add_profile(services, "boboneshot", "sharedoneshot", suspended=False)
        oneshot = IrcClient("127.0.0.1", client_port, "oneshot-client")
        clients.append(oneshot)
        oneshot.request("NICK aliceoneshot:sharedoneshot", lambda line: " NICK :aliceoneshot" in line,
                        "one-shot source identification")
        wait_for_mode(oneshot, "aliceoneshot", "+r", "one-shot source +r")
        oneshot.request("NICK oneshotfree", lambda line: " NICK :oneshotfree" in line, "one-shot free nick")
        stale = request(oneshot, "NICK boboneshot", lambda line: " 432 " in line, "stale password rejection")
        require(any("requires a password" in line for line in stale),
                f"stale password was not rejected as a missing credential: {stale!r}")

        add_profile(services, "aliceflood", "alicefloodsecret", suspended=False)
        add_profile(services, "bobflood", "bobfloodsecret", suspended=False)
        flood = IrcClient("127.0.0.1", client_port, "flood-client")
        clients.append(flood)
        flood.request("NICK aliceflood:alicefloodsecret", lambda line: " NICK :aliceflood" in line,
                      "flood source identification")
        wait_for_mode(flood, "aliceflood", "+r", "flood source +r")
        flood.request("NICK floodfree", lambda line: " NICK :floodfree" in line, "flood free nick")
        stale_attempt = request(flood, "NICK bobflood", lambda line: " 432 " in line, "stale flood attempt")
        require(not any("Invalid password for bobflood" in line for line in stale_attempt),
                f"stale credential consumed another profile's password check: {stale_attempt!r}")
        flood.request("NICK bobflood:bobfloodsecret", lambda line: " NICK :bobflood" in line,
                      "explicit flood destination authentication")
        wait_for_mode(flood, "bobflood", "+r", "explicit flood destination +r", timeout=10)

        add_profile(services, "sneak", "sneaksecretstale")
        sneak_holder = IrcClient("127.0.0.1", client_port, "sneak-holder")
        clients.append(sneak_holder)
        sneak_holder.request("NICK sneak:sneaksecretstale", lambda line: " NICK :sneak" in line,
                             "sneak holder adoption")
        sneak_holder.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                              "sneak holder suspension notice", timeout=10)
        add_profile(services, "victimproof", "victimproofsecret")
        victim = adopt_suspended("127.0.0.1", client_port, "victimproof", "victimproofsecret")
        clients.append(victim)
        victim.request("NICK sneak:sneaksecretstale", lambda line: " 433 " in line,
                       "occupied suspended target rejection")
        current = current_nick(victim)
        sneak_holder.close()
        clients.remove(sneak_holder)
        time.sleep(0.5)
        services.send(f"SVSNICK {current} sneak {int(time.time())}")
        free_guest(victim, "forced rename after failed attempt")
        sneak_nick = current_nick(victim)
        assert_unidentified(victim, sneak_nick, "forced rename after failed attempt", timeout=10)
        start = len(victim.lines)
        services.send_del("N::sneak::suspend")
        time.sleep(0.8)
        events = victim.lines[start:]
        require(not any(" MODE " in line and "+r" in line for line in events),
                f"stale pending credential was promoted by SVSNICK: {events!r}")

        # U: a hot access denial for a passless profile renames but must not
        # strip externally owned state.
        services.send_ins("N::denyinert::access", "127.0.0.0/8")
        services.send_ins("N::denyinert::modes", "+S")
        time.sleep(0.25)
        deniedext = IrcClient("127.0.0.1", client_port, "denyinert-external")
        clients.append(deniedext)
        services.send("SVS2MODE denyinert-external +S")
        wait_for_mode(deniedext, "denyinert-external", "+S", "external +S before hot access denial")
        deniedext.request("NICK denyinert", lambda line: " NICK :denyinert" in line, "hot deny passless nick")
        start = len(deniedext.lines)
        services.send_ins("N::denyinert::access", "192.0.2.0/24")
        renamed = free_guest(deniedext, "passless hot access rename", start=start)
        require(has_usermode(deniedext, renamed, "S", timeout=10),
                "hot passless access denial stripped external +S")

        # U2: a full passless snapshot must not revoke externally owned state,
        # even when the profile is replaced or removed.
        services.send_ins("N::snapinert::access", "127.0.0.0/8")
        services.send_ins("N::snapinert::vhost", "snapinert.test")
        services.send_ins("N::snapgone::access", "127.0.0.0/8")
        services.send_ins("N::snapgone::vhost", "snapgone.test")
        time.sleep(0.3)
        snapinertc = IrcClient("127.0.0.1", client_port, "snapinert-external")
        clients.append(snapinertc)
        services.send("SVS2MODE snapinert-external +S")
        services.send("CHGHOST snapinert-external external-snapinert.test")
        wait_for_mode(snapinertc, "snapinert-external", "+S", "external state before passless snapshot")
        snapinertc.wait_for(lambda line: " 396 " in line and "external-snapinert.test" in line,
                            "external vhost before passless snapshot", timeout=15)
        snapinertc.request("NICK snapinert", lambda line: " NICK :snapinert" in line, "passless snapshot keep")
        snapgonec = IrcClient("127.0.0.1", client_port, "snapgone-external")
        clients.append(snapgonec)
        services.send("SVS2MODE snapgone-external +S")
        services.send("CHGHOST snapgone-external external-snapgone.test")
        wait_for_mode(snapgonec, "snapgone-external", "+S", "external state before removed snapshot")
        snapgonec.wait_for(lambda line: " 396 " in line and "external-snapgone.test" in line,
                           "external vhost before removed snapshot", timeout=15)
        snapgonec.request("NICK snapgone", lambda line: " NICK :snapgone" in line, "passless snapshot remove")
        replace_n_tree(services, 111, "nick-passless-inert-h",
                       [("snapinert::access", "127.0.0.0/8"),
                        ("snapinert::vhost", "snapinert-replaced.test")])
        wait_for_db_records(n_db, ("snapinert-replaced.test",), ("snapgone::vhost",))
        time.sleep(0.3)
        require(has_usermode(snapinertc, "snapinert", "S", timeout=15) and
                has_usermode(snapgonec, "snapgone", "S", timeout=15),
                "passless snapshot revoked externally owned +S")
        kept_whois = request(snapinertc, "WHOIS snapinert", lambda line: " 318 " in line,
                             "snapshot passless external vhost", timeout=15)
        require(any("external-snapinert.test" in line for line in kept_whois) and
                not any("snapinert-replaced.test" in line for line in kept_whois),
                f"passless snapshot replaced external vhost: {kept_whois!r}")
        gone_whois = request(snapgonec, "WHOIS snapgone", lambda line: " 318 " in line,
                             "removed passless snapshot external vhost", timeout=15)
        require(any("external-snapgone.test" in line for line in gone_whois) and
                not any("snapgone.test" in line and "external-snapgone.test" not in line
                        for line in gone_whois),
                f"removed passless snapshot revoked external vhost: {gone_whois!r}")

        for client in clients:
            client.close()
        clients.clear()
        time.sleep(0.4)

        # V: first NICK registration follows the same identity rules; a first
        # registration onto a suspended profile keeps no identity.
        add_profile(services, "firstowner", "firstownersecret", suspended=False)
        firstowner = RegistrationClient("127.0.0.1", client_port, "firstowner", "firstownersecret")
        clients.append(firstowner)
        wait_for_mode(firstowner, "firstowner", "+r", "first NICK +r", timeout=10)
        first_whois = firstowner.request("WHOIS firstowner", lambda line: " 318 " in line, "first NICK vhost")
        require(any("firstowner.test" in line for line in first_whois),
                f"first NICK did not apply profile effects: {first_whois!r}")

        add_profile(services, "firstheld", "firstheldsecret")
        firstheld = RegistrationClient("127.0.0.1", client_port, "firstheld", "firstheldsecret")
        clients.append(firstheld)
        firstheld.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                           "first NICK suspended notice", timeout=10)
        assert_unidentified(firstheld, "firstheld", "first NICK suspended holder", timeout=10)
        start = len(firstheld.lines)
        services.send_del("N::firstheld::suspend")
        free_guest(firstheld, "first NICK suspended DEL rename", start=start)
        assert_unidentified(firstheld, current_nick(firstheld), "first NICK unsuspended holder")

        # W: exact mode ownership. UDB records only the bits it changed 0->1.
        add_effect_profile(services, "modeown", "modeownsecret")
        modeown = IrcClient("127.0.0.1", client_port, "modeown-client")
        clients.append(modeown)
        authenticate(modeown, "modeown", "modeownsecret", "mode owner identification")
        services.send_ins("N::modeown::modes", "+S")
        wait_for_mode(modeown, "modeown", "+S", "UDB-added +S")
        start = len(modeown.lines)
        services.send_ins("N::modeown::suspend", "manual review")
        modeown.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                         "mode owner suspend notice", start=start, timeout=15)
        require(not has_usermode(modeown, "modeown", "S", timeout=15),
                "suspend kept a mode UDB itself added")
        require(current_nick(modeown) == "modeown", "mode owner suspend renamed the holder")

        add_effect_profile(services, "modeext", "modeextsecret", (("modes", "+S"),))
        modeext = IrcClient("127.0.0.1", client_port, "modeext-client")
        clients.append(modeext)
        services.send("SVS2MODE modeext-client +S")
        wait_for_mode(modeext, "modeext-client", "+S", "external +S before mode auth")
        modeext.request("NICK modeext:modeextsecret", lambda line: " NICK :modeext" in line,
                        "mode external owner identification")
        wait_for_mode(modeext, "modeext", "+r", "mode external owner +r")
        start = len(modeext.lines)
        services.send_ins("N::modeext::suspend", "manual review")
        modeext.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                         "mode external suspend notice", start=start, timeout=15)
        require(has_usermode(modeext, "modeext", "S", timeout=15),
                "suspend removed an externally owned mode listed by the profile")

        add_effect_profile(services, "modegone", "modegonesecret", (("modes", "+S"),))
        modegone = IrcClient("127.0.0.1", client_port, "modegone-client")
        clients.append(modegone)
        authenticate(modegone, "modegone", "modegonesecret", "mode gone identification")
        wait_for_mode(modegone, "modegone", "+S", "mode gone +S")
        services.send("SVS2MODE modegone -S")
        wait_for_mode(modegone, "modegone", "-S", "external +S removal")
        start = len(modegone.lines)
        services.send_ins("N::modegone::suspend", "manual review")
        modegone.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                          "mode gone suspend notice", start=start, timeout=15)
        require(not has_usermode(modegone, "modegone", "S", timeout=15),
                "revoking an externally removed mode failed")

        add_effect_profile(services, "modedel", "modedelsecret", (("modes", "+S"),))
        modedel = IrcClient("127.0.0.1", client_port, "modedel-client")
        clients.append(modedel)
        authenticate(modedel, "modedel", "modedelsecret", "mode delete identification")
        wait_for_mode(modedel, "modedel", "+S", "mode delete +S")
        services.send_del("N::modedel::modes")
        wait_for_mode(modedel, "modedel", "-S", "hot modes deletion")
        require(has_usermode(modedel, "modedel", "r", timeout=15),
                "hot modes deletion revoked identity")

        # X: exact vhost ownership. UDB only removes a vhost while the current
        # value is still the one it applied.
        add_effect_profile(services, "vown", "vownsecret", (("vhost", "vown.test"),))
        vown = IrcClient("127.0.0.1", client_port, "vown-client")
        clients.append(vown)
        authenticate(vown, "vown", "vownsecret", "vhost owner identification")
        vown_whois = request(vown, "WHOIS vown", lambda line: " 318 " in line, "vhost owner WHOIS", timeout=15)
        require(any("vown.test" in line for line in vown_whois), f"vhost was not applied: {vown_whois!r}")
        start = len(vown.lines)
        services.send_ins("N::vown::suspend", "manual review")
        vown.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "vhost owner suspend notice", start=start, timeout=15)
        vown_whois = request(vown, "WHOIS vown", lambda line: " 318 " in line, "vhost owner revoked WHOIS",
                             timeout=15)
        require(not any("vown.test" in line for line in vown_whois),
                f"suspend kept the UDB-owned vhost: {vown_whois!r}")
        require(current_nick(vown) == "vown", "vhost owner suspend renamed the holder")

        add_effect_profile(services, "vext", "vextsecret", (("vhost", "vext.test"),))
        vext = IrcClient("127.0.0.1", client_port, "vext-external")
        clients.append(vext)
        services.send("CHGHOST vext-external vext.test")
        vext.wait_for(lambda line: " 396 " in line and "vext.test" in line,
                      "external vhost equal to profile value", timeout=15)
        vext.request("NICK vext:vextsecret", lambda line: " NICK :vext" in line, "vhost match identification")
        wait_for_mode(vext, "vext", "+r", "vhost match +r")
        start = len(vext.lines)
        services.send_ins("N::vext::suspend", "manual review")
        vext.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "vhost match suspend notice", start=start, timeout=15)
        vext_whois = request(vext, "WHOIS vext", lambda line: " 318 " in line, "vhost match WHOIS", timeout=15)
        require(any("vext.test" in line for line in vext_whois),
                f"suspend removed an externally supplied matching vhost: {vext_whois!r}")

        add_effect_profile(services, "vover", "voversecret", (("vhost", "vover.test"),))
        vover = IrcClient("127.0.0.1", client_port, "vover-client")
        clients.append(vover)
        authenticate(vover, "vover", "voversecret", "vhost override identification")
        services.send("CHGHOST vover vover-ext.test")
        vover.wait_for(lambda line: " 396 " in line and "vover-ext.test" in line,
                       "external vhost override", timeout=15)
        start = len(vover.lines)
        services.send_ins("N::vover::suspend", "manual review")
        vover.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "vhost override suspend notice", start=start, timeout=15)
        vover_whois = request(vover, "WHOIS vover", lambda line: " 318 " in line, "vhost override WHOIS",
                              timeout=15)
        require(any("vover-ext.test" in line for line in vover_whois) and
                not any("vover.test" in line and "vover-ext.test" not in line for line in vover_whois),
                f"suspend replaced an externally overridden vhost: {vover_whois!r}")

        add_effect_profile(services, "vhot", "vhotsecret", (("vhost", "vhot-a.test"),))
        vhot = IrcClient("127.0.0.1", client_port, "vhot-client")
        clients.append(vhot)
        authenticate(vhot, "vhot", "vhotsecret", "vhost hot identification")
        services.send_ins("N::vhot::vhost", "vhot-b.test")
        time.sleep(0.4)
        vhot_whois = request(vhot, "WHOIS vhot", lambda line: " 318 " in line, "vhost hot WHOIS", timeout=15)
        require(any("vhot-b.test" in line for line in vhot_whois) and
                not any("vhot-a.test" in line for line in vhot_whois),
                f"hot vhost update did not replace the owned value: {vhot_whois!r}")
        start = len(vhot.lines)
        services.send_ins("N::vhot::suspend", "manual review")
        vhot.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "vhost hot suspend notice", start=start, timeout=15)
        vhot_whois = request(vhot, "WHOIS vhot", lambda line: " 318 " in line, "vhost hot revoked WHOIS",
                             timeout=15)
        require(not any("vhot-b.test" in line for line in vhot_whois),
                f"suspend kept a replaced-but-still-owned vhost: {vhot_whois!r}")

        add_effect_profile(services, "vmix", "vmixsecret")
        vmix = IrcClient("127.0.0.1", client_port, "vmix-external")
        clients.append(vmix)
        services.send("CHGHOST vmix-external vmix-ext.test")
        vmix.wait_for(lambda line: " 396 " in line and "vmix-ext.test" in line,
                      "external vhost before explicit profile vhost", timeout=15)
        services.send_ins("N::vmix::vhost", "vmix-udb.test")
        vmix.request("NICK vmix:vmixsecret", lambda line: " NICK :vmix" in line, "vhost mix identification")
        wait_for_mode(vmix, "vmix", "+r", "vhost mix +r")
        vmix_whois = request(vmix, "WHOIS vmix", lambda line: " 318 " in line, "vhost mix WHOIS", timeout=15)
        require(any("vmix-udb.test" in line for line in vmix_whois),
                f"hot profile vhost was not applied over external state: {vmix_whois!r}")

        # Y: snomask ownership with restore/preserve semantics.
        add_effect_profile(services, "sown", "sownsecret", (("snomasks", "c"),))
        sown = IrcClient("127.0.0.1", client_port, "sown-client")
        clients.append(sown)
        authenticate(sown, "sown", "sownsecret", "snomask owner identification")
        value = read_snomask(services, sown, "sown", "snomask applied")
        require("c" in value, f"UDB snomask was not applied: {value!r}")
        start = len(sown.lines)
        services.send_ins("N::sown::suspend", "manual review")
        sown.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "snomask owner suspend notice", start=start, timeout=15)
        value = read_snomask(services, sown, "sown", "snomask revoked")
        require(value == "", f"suspend kept a UDB-owned snomask: {value!r}")

        add_effect_profile(services, "sext", "sextsecret", (("snomasks", "+c-k"),))
        sext = IrcClient("127.0.0.1", client_port, "sext-client")
        clients.append(sext)
        set_external_snomask(services, "sext-client", "k")
        time.sleep(0.3)
        sext.request("NICK sext:sextsecret", lambda line: " NICK :sext" in line, "snomask restore identification")
        wait_for_mode(sext, "sext", "+r", "snomask restore +r")
        value = read_snomask(services, sext, "sext", "snomask restore applied")
        require("c" in value and "k" not in value, f"UDB snomask did not replace external state: {value!r}")
        start = len(sext.lines)
        services.send_ins("N::sext::suspend", "manual review")
        sext.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "snomask restore suspend notice", start=start, timeout=15)
        value = read_snomask(services, sext, "sext", "snomask restored external")
        require("k" in value and "c" not in value, f"suspend did not restore the external snomask: {value!r}")

        add_effect_profile(services, "ssame", "ssamesecret", (("snomasks", "c"),))
        ssame = IrcClient("127.0.0.1", client_port, "ssame-client")
        clients.append(ssame)
        set_external_snomask(services, "ssame-client", "c")
        time.sleep(0.3)
        ssame.request("NICK ssame:ssamesecret", lambda line: " NICK :ssame" in line, "snomask same identification")
        wait_for_mode(ssame, "ssame", "+r", "snomask same +r")
        start = len(ssame.lines)
        services.send_ins("N::ssame::suspend", "manual review")
        ssame.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask same suspend notice", start=start, timeout=15)
        value = read_snomask(services, ssame, "ssame", "snomask same revoked")
        require("c" in value, f"suspend removed an externally supplied matching snomask: {value!r}")

        add_effect_profile(services, "sover", "soversecret", (("snomasks", "c"),))
        sover = IrcClient("127.0.0.1", client_port, "sover-client")
        clients.append(sover)
        authenticate(sover, "sover", "soversecret", "snomask override identification")
        set_external_snomask(services, "sover", "k")
        time.sleep(0.3)
        start = len(sover.lines)
        services.send_ins("N::sover::suspend", "manual review")
        sover.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask override suspend notice", start=start, timeout=15)
        value = read_snomask(services, sover, "sover", "snomask override revoked")
        require("k" in value and "c" not in value, f"suspend overwrote an external snomask: {value!r}")

        add_effect_profile(services, "snone", "snonessecret")
        snone = IrcClient("127.0.0.1", client_port, "snone-client")
        clients.append(snone)
        set_external_snomask(services, "snone-client", "k")
        time.sleep(0.3)
        snone.request("NICK snone:snonessecret", lambda line: " NICK :snone" in line, "snomask absent identification")
        wait_for_mode(snone, "snone", "+r", "snomask absent +r")
        start = len(snone.lines)
        services.send_ins("N::snone::suspend", "manual review")
        snone.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask absent suspend notice", start=start, timeout=15)
        value = read_snomask(services, snone, "snone", "snomask absent revoked")
        require("k" in value, f"a profile without snomasks removed external state: {value!r}")

        # Y1: relative snomask expression +c-k with external k -> result c
        add_effect_profile(services, "srel1", "srel1secret", (("snomasks", "+c-k"),))
        srel1 = IrcClient("127.0.0.1", client_port, "srel1-client")
        clients.append(srel1)
        set_external_snomask(services, "srel1-client", "k")
        time.sleep(0.3)
        srel1.request("NICK srel1:srel1secret", lambda line: " NICK :srel1" in line, "snomask +c-k identification")
        wait_for_mode(srel1, "srel1", "+r", "snomask +c-k +r")
        val = read_snomask(services, srel1, "srel1", "snomask +c-k applied")
        require("c" in val and "k" not in val, f"+c-k on external k did not result in c only: {val!r}")
        start = len(srel1.lines)
        services.send_ins("N::srel1::suspend", "manual review")
        srel1.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask +c-k suspend notice", start=start, timeout=15)
        val = read_snomask(services, srel1, "srel1", "snomask +c-k revoked")
        require("k" in val and "c" not in val, f"suspend did not restore external k: {val!r}")

        # Y2: relative snomask expression -k with external ck -> result c
        add_effect_profile(services, "srel2", "srel2secret", (("snomasks", "-k"),))
        srel2 = IrcClient("127.0.0.1", client_port, "srel2-client")
        clients.append(srel2)
        set_external_snomask(services, "srel2-client", "ck")
        time.sleep(0.3)
        srel2.request("NICK srel2:srel2secret", lambda line: " NICK :srel2" in line, "snomask -k identification")
        wait_for_mode(srel2, "srel2", "+r", "snomask -k +r")
        val = read_snomask(services, srel2, "srel2", "snomask -k applied")
        require("c" in val and "k" not in val, f"-k on external ck did not result in c only: {val!r}")
        start = len(srel2.lines)
        services.send_ins("N::srel2::suspend", "manual review")
        srel2.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask -k suspend notice", start=start, timeout=15)
        val = read_snomask(services, srel2, "srel2", "snomask -k revoked")
        require("c" in val and "k" in val, f"suspend did not restore external ck: {val!r}")

        # Y3: relative snomask expression +c with no initial snomasks -> result c
        add_effect_profile(services, "srel3", "srel3secret", (("snomasks", "+c"),))
        srel3 = IrcClient("127.0.0.1", client_port, "srel3-client")
        clients.append(srel3)
        authenticate(srel3, "srel3", "srel3secret", "snomask +c identification")
        val = read_snomask(services, srel3, "srel3", "snomask +c applied")
        require("c" in val, f"+c did not apply c: {val!r}")
        start = len(srel3.lines)
        services.send_ins("N::srel3::suspend", "manual review")
        srel3.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "snomask +c suspend notice", start=start, timeout=15)
        val = read_snomask(services, srel3, "srel3", "snomask +c revoked")
        require(val == "", f"suspend kept UDB snomask +c: {val!r}")

        # Y4: external oper with different operclass: UDB must not touch it
        add_effect_profile(services, "opdiff", "opdiffsecret", (("oper", "netadmin"),))
        opdiff = IrcClient("127.0.0.1", client_port, "opdiff-client")
        clients.append(opdiff)
        opdiff.request("OPER testoper operpass", lambda line: " 381 " in line, "external oper login")
        st_before = read_oper_state(services, opdiff, "opdiff-client", "external oper state before auth")
        require(st_before["oper"] and st_before["operclass"] == "locop",
                f"external oper did not become locop: {st_before!r}")
        opdiff.request("NICK opdiff:opdiffsecret", lambda line: " NICK :opdiff" in line, "oper diff identification")
        wait_for_mode(opdiff, "opdiff", "+r", "oper diff +r")
        st_after = read_oper_state(services, opdiff, "opdiff", "oper state after auth")
        require(st_after["oper"] and st_after["operclass"] == "locop",
                f"UDB hijacked external oper or altered operclass: {st_after!r}")
        start = len(opdiff.lines)
        services.send_ins("N::opdiff::suspend", "manual review")
        opdiff.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                        "oper diff suspend notice", start=start, timeout=15)
        st_susp = read_oper_state(services, opdiff, "opdiff", "oper state after suspend")
        require(st_susp["oper"] and st_susp["operclass"] == "locop",
                f"suspend de-opered external oper: {st_susp!r}")

        # Y5: external oper with same operclass: UDB must not claim ownership
        add_effect_profile(services, "opsame", "opsamesecret", (("oper", "locop"),))
        opsame = IrcClient("127.0.0.1", client_port, "opsame-client")
        clients.append(opsame)
        opsame.request("OPER testoper operpass", lambda line: " 381 " in line, "external oper same login")
        opsame.request("NICK opsame:opsamesecret", lambda line: " NICK :opsame" in line, "oper same identification")
        wait_for_mode(opsame, "opsame", "+r", "oper same +r")
        start = len(opsame.lines)
        services.send_ins("N::opsame::suspend", "manual review")
        opsame.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                        "oper same suspend notice", start=start, timeout=15)
        st_same = read_oper_state(services, opsame, "opsame", "oper state after suspend same")
        require(st_same["oper"] and st_same["operclass"] == "locop",
                f"suspend de-opered matching external oper: {st_same!r}")

        # Y6: UDB-granted oper and clean revocation
        add_effect_profile(services, "opudb", "opudbsecret", (("oper", "locop"),))
        opudb = IrcClient("127.0.0.1", client_port, "opudb-client")
        clients.append(opudb)
        st_start = read_oper_state(services, opudb, "opudb-client", "opudb initial")
        require(not st_start["oper"], f"opudb was already oper: {st_start!r}")
        count_before = st_start["opercount"]
        authenticate(opudb, "opudb", "opudbsecret", "opudb identification")
        wait_for_mode(opudb, "opudb", "+o", "opudb +o")
        st_grant = read_oper_state(services, opudb, "opudb", "opudb granted")
        require(st_grant["oper"] and st_grant["operclass"] == "locop",
                f"UDB did not grant locop: {st_grant!r}")
        require(st_grant["opercount"] == count_before + 1,
                f"opercount not incremented: before={count_before} grant={st_grant['opercount']}")
        require(st_grant["operlist"] == 1,
                f"operlist duplicate or missing: {st_grant['operlist']}")
        start = len(opudb.lines)
        services.send_ins("N::opudb::suspend", "manual review")
        opudb.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                       "opudb suspend notice", start=start, timeout=15)
        st_rev = read_oper_state(services, opudb, "opudb", "opudb revoked")
        require(not st_rev["oper"] and st_rev["operclass"] == "-",
                f"suspend did not revoke UDB oper: {st_rev!r}")
        require(st_rev["opercount"] == count_before,
                f"opercount not restored: before={count_before} rev={st_rev['opercount']}")
        require(st_rev["operlist"] == 0,
                f"operlist entry left after deoper: {st_rev['operlist']}")

        # Z: external account/+r during an active identity never recreates auth,
        # and identity revocation clears the public projection it owned.
        add_effect_profile(services, "acctci", "acctcisecret")
        acctci = IrcClient("127.0.0.1", client_port, "acctci-client")
        clients.append(acctci)
        authenticate(acctci, "acctci", "acctcisecret", "external account during identity")
        services.send("SVSLOGIN * acctci other")
        services.send("SVS2MODE acctci -r")
        time.sleep(0.3)
        start = len(acctci.lines)
        services.send_ins("N::acctci::suspend", "manual review")
        acctci.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                        "external account suspend notice", start=start, timeout=15)
        require(not has_usermode(acctci, "acctci", "r", timeout=15),
                "external account state survived identity revocation")
        acctci_whois = request(acctci, "WHOIS acctci", lambda line: " 318 " in line,
                               "external account revoked WHOIS", timeout=15)
        require(not any(" 330 " in line and "other" in line for line in acctci_whois),
                f"external account was not cleared on revoke: {acctci_whois!r}")
        start = len(acctci.lines)
        services.send_del("N::acctci::suspend")
        free_guest(acctci, "external account unsuspend rename", start=start)
        authenticate(acctci, "acctci", "acctcisecret", "external account reauthentication")
        acctci_whois = request(acctci, "WHOIS acctci", lambda line: " 318 " in line,
                               "external account reauth WHOIS", timeout=15)
        require(any(" 330 " in line and "acctci" in line for line in acctci_whois),
                f"explicit reauthentication did not restore the account: {acctci_whois!r}")

        # AA1: auth -> external vhost -> suspend -> unsuspend -> reauth.
        add_effect_profile(services, "seqone", "seqonesecret", (("vhost", "seqone-a.test"),))
        seqone = IrcClient("127.0.0.1", client_port, "seqone-client")
        clients.append(seqone)
        authenticate(seqone, "seqone", "seqonesecret", "sequence one identification")
        services.send("CHGHOST seqone seq-ext.test")
        seqone.wait_for(lambda line: " 396 " in line and "seq-ext.test" in line,
                        "sequence one external vhost", timeout=15)
        start = len(seqone.lines)
        services.send_ins("N::seqone::suspend", "manual review")
        seqone.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                        "sequence one suspend notice", start=start, timeout=15)
        seqone_whois = request(seqone, "WHOIS seqone", lambda line: " 318 " in line,
                               "sequence one suspended WHOIS", timeout=15)
        require(any("seq-ext.test" in line for line in seqone_whois) and
                not any("seqone-a.test" in line for line in seqone_whois),
                f"sequence one suspend did not preserve the external vhost: {seqone_whois!r}")
        start = len(seqone.lines)
        services.send_del("N::seqone::suspend")
        free_guest(seqone, "sequence one unsuspend rename", start=start)
        authenticate(seqone, "seqone", "seqonesecret", "sequence one reauthentication")
        seqone_whois = request(seqone, "WHOIS seqone", lambda line: " 318 " in line,
                               "sequence one reauth WHOIS", timeout=15)
        require(any("seqone-a.test" in line for line in seqone_whois),
                f"sequence one reauthentication did not reapply the profile: {seqone_whois!r}")

        # AA2: auth -> external +S -> modes update -> suspend keeps the external
        # bit and revokes only the newly added one.
        add_effect_profile(services, "seqmode", "seqmodesecret", (("modes", "+S"),))
        seqmode = IrcClient("127.0.0.1", client_port, "seqmode-client")
        clients.append(seqmode)
        services.send("SVS2MODE seqmode-client +S")
        wait_for_mode(seqmode, "seqmode-client", "+S", "sequence modes external +S")
        seqmode.request("NICK seqmode:seqmodesecret", lambda line: " NICK :seqmode" in line,
                        "sequence modes identification")
        wait_for_mode(seqmode, "seqmode", "+r", "sequence modes +r")
        services.send_ins("N::seqmode::modes", "+SD")
        wait_for_mode(seqmode, "seqmode", "+D", "sequence modes update")
        start = len(seqmode.lines)
        services.send_ins("N::seqmode::suspend", "manual review")
        seqmode.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                         "sequence modes suspend notice", start=start, timeout=15)
        require(has_usermode(seqmode, "seqmode", "S", timeout=15),
                "sequence modes suspend removed the external bit")
        require(not has_usermode(seqmode, "seqmode", "D", timeout=15),
                "sequence modes suspend kept the UDB-added bit")

        # AA3: failed NICK -> SVSNICK -> policy mutation never authenticates.
        add_effect_profile(services, "seqfail", "seqfailsecret")
        seqfail = IrcClient("127.0.0.1", client_port, "seqfail-guest")
        clients.append(seqfail)
        denied = seqfail.request("NICK seqfail:wrong", lambda line: any(code in line for code in (" 432 ", " 433 ")),
                                 "sequence failed credential")
        require(any("password" in line.lower() for line in denied),
                f"sequence failed credential was not denied: {denied!r}")
        services.send(f"SVSNICK seqfail-guest seqfail {int(time.time())}")
        free_guest(seqfail, "sequence forced rename")
        guest = current_nick(seqfail)
        services.send_ins("N::seqfail::suspend", "manual review")
        services.send_del("N::seqfail::suspend")
        time.sleep(0.5)
        require(not has_usermode(seqfail, guest, "r", timeout=15),
                "sequence policy mutation authenticated a forced rename")

        for client in clients:
            client.close()
        clients.clear()
        time.sleep(0.4)

        # AB: snapshot effects update -> external override -> profile delete.
        # External state must survive the deletion while UDB-owned state and
        # identity are revoked.
        add_effect_profile(services, "snapseq", "snapseqsecret",
                           (("vhost", "snapseq-a.test"), ("modes", "+S")))
        snapseq = IrcClient("127.0.0.1", client_port, "snapseq-client")
        clients.append(snapseq)
        authenticate(snapseq, "snapseq", "snapseqsecret", "ownership sequence identification")
        snap_records = [("snapseq::pass", "sha256:" + sha256("snapseqsecret")),
                        ("snapseq::access", "127.0.0.0/8"),
                        ("snapseq::vhost", "snapseq-b.test"),
                        ("snapseq::modes", "+S")]
        replace_n_tree(services, 120, "ownership-sequence-a", snap_records)
        wait_for_db_records(n_db, ("snapseq::vhost snapseq-b.test",))
        time.sleep(0.3)
        require(has_usermode(snapseq, "snapseq", "r", timeout=15),
                "equivalent ownership snapshot revoked identity")
        snapseq_whois = request(snapseq, "WHOIS snapseq", lambda line: " 318 " in line,
                                "ownership sequence WHOIS", timeout=15)
        require(any("snapseq-b.test" in line for line in snapseq_whois),
                f"ownership sequence snapshot did not update the vhost: {snapseq_whois!r}")
        services.send("CHGHOST snapseq external-seq.test")
        snapseq.wait_for(lambda line: " 396 " in line and "external-seq.test" in line,
                         "ownership sequence external override", timeout=15)
        replace_n_tree(services, 121, "ownership-sequence-b", [("other::access", "127.0.0.0/8")])
        wait_for_db_records(n_db, ("other::access 127.0.0.0/8",), ("snapseq::pass", "snapseq::vhost"))
        time.sleep(0.3)
        require(current_nick(snapseq) == "snapseq", "ownership sequence deletion renamed the holder")
        require(not has_usermode(snapseq, "snapseq", "r", timeout=15),
                "ownership sequence deletion kept identity")
        require(not has_usermode(snapseq, "snapseq", "S", timeout=15),
                "ownership sequence deletion kept a UDB-owned mode")
        snapseq_whois = request(snapseq, "WHOIS snapseq", lambda line: " 318 " in line,
                                "ownership sequence deleted WHOIS", timeout=15)
        require(any("external-seq.test" in line for line in snapseq_whois),
                f"ownership sequence deletion removed external vhost state: {snapseq_whois!r}")

        print("PASS: identity, effects ownership, suspend revocation, snapshots, SVSNICK and one-shot nick auth")
    finally:
        for client in clients:
            client.close()
        if services:
            services.close()
        if process:
            stop(process)
        if stdout_handle:
            stdout_handle.close()
        if not keep:
            shutil.rmtree(root, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if not args.ircd.is_file() or not os.access(args.ircd, os.X_OK):
        return skip(f"UnrealIRCd binary is unavailable: {args.ircd}")
    if not args.module.is_file():
        return skip(f"compiled UDB module is unavailable: {args.module}")
    if not (RUNTIME_ROOT / "conf/modules.default.conf").is_file():
        return skip(f"installed modules.default.conf is unavailable under {RUNTIME_ROOT}")
    try:
        run_tests(args.ircd, args.module, args.keep)
    except EnvironmentUnavailable as error:
        return skip(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())
