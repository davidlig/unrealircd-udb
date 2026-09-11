#!/usr/bin/env python3
"""Runtime contract for retained, policy-bound N::suspend authentication."""

import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

from runtime_schema_validation import (
    DEFAULT_IRCD,
    EnvironmentUnavailable,
    FakeServicesServer,
    IRCD_SID,
    IrcClient,
    RUNTIME_ROOT,
    bwrap_command,
    find_module_path,
    free_port,
    run_configtest,
    skip,
    stop,
    write_config,
)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(value):
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def wait_for_mode(client, nick, required, description, start=0):
    lines = client.wait_for(lambda line: f" MODE {nick} " in line and required in line,
                            description, start=start)
    require(any(required in line for line in lines), f"{description}: {lines!r}")


def assert_unidentified(client, nick, description):
    modes = client.request(f"MODE {nick}", lambda line: " 221 " in line, description)
    require(not any("+r" in line for line in modes), f"{description}: +r leaked: {modes!r}")
    whois = client.request(f"WHOIS {nick}", lambda line: " 318 " in line, description + " WHOIS")
    require(not any(f"{nick}.test" in line for line in whois), f"{description}: vhost leaked: {whois!r}")


def add_profile(services, nick, password, suspended=True):
    services.send_ins(f"N::{nick}::pass", "sha256:" + sha256(password))
    services.send_ins(f"N::{nick}::challenge", "sha256")
    services.send_ins(f"N::{nick}::access", "127.0.0.0/8")
    services.send_ins(f"N::{nick}::vhost", f"{nick}.test")
    if suspended:
        services.send_ins(f"N::{nick}::suspend", "manual review")
    time.sleep(0.25)


def authenticate_suspended(host, port, nick, password):
    client = IrcClient(host, port, nick + "-client")
    client.request(f"NICK {nick}:{password}", lambda line: f" NICK :{nick}" in line,
                   "authenticated suspended nick")
    client.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "suspension reason")
    assert_unidentified(client, nick, "suspended authenticated client")
    return client


def run_tests(ircd, module, keep=False):
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-nick-suspend-auth-"))
    process = None
    services = None
    clients = []
    try:
        node = root / "node"
        for path in (node / "runtime-data", node / "tmp", node / "modules" / "third"):
            path.mkdir(parents=True, exist_ok=True)
        shutil.copy2(module, node / "modules" / "third" / "udb.so")
        client_port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "ircd.test", IRCD_SID, client_port, server_port, tls_port,
                     node / "modules" / "third" / "udb.so", node / "runtime-data")
        run_configtest(node, ircd, config)
        process = subprocess.Popen(bwrap_command(node, ircd, config), stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        time.sleep(1)
        if process.poll() is not None:
            output, _ = process.communicate()
            raise RuntimeError(f"ircd exited immediately:\n{output}")
        services = FakeServicesServer("127.0.0.1", server_port)

        # A: normal owner -> hot suspend -> hot DEL restores public identity/effects.
        add_profile(services, "owner", "ownersecret", suspended=False)
        owner = IrcClient("127.0.0.1", client_port, "owner-client")
        clients.append(owner)
        owner.request("NICK owner:ownersecret", lambda line: " NICK :owner" in line, "owner identification")
        wait_for_mode(owner, "owner", "+r", "owner +r")
        services.send_ins("N::owner::suspend", "manual review")
        owner.wait_for(lambda line: "This nickname is suspended. Reason: manual review" in line, "hot suspend notice")
        assert_unidentified(owner, "owner", "hot suspended owner")
        start = len(owner.lines)
        services.send_del("N::owner::suspend")
        wait_for_mode(owner, "owner", "+r", "hot unsuspend restores +r", start=start)
        whois = owner.request("WHOIS owner", lambda line: " 318 " in line, "restored owner WHOIS")
        require(any("owner.test" in line for line in whois), f"hot unsuspend did not restore vhost: {whois!r}")

        # B: authentication performed while already suspended needs no second password after DEL.
        add_profile(services, "held", "heldsecret")
        held = authenticate_suspended("127.0.0.1", client_port, "held", "heldsecret")
        clients.append(held)
        start = len(held.lines)
        services.send_del("N::held::suspend")
        wait_for_mode(held, "held", "+r", "held unsuspend restores +r", start=start)
        whois = held.request("WHOIS held", lambda line: " 318 " in line, "held restored WHOIS")
        require(any("held.test" in line for line in whois), f"held unsuspend did not restore vhost: {whois!r}")

        # C: each authentication-policy field invalidates retained authentication.
        for nick, field, value in (
            ("passchanged", "pass", "sha256:" + sha256("newsecret")),
            ("challengechanged", "challenge", "crypt"),
            ("accesschanged", "access", "192.0.2.0/24"),
        ):
            add_profile(services, nick, "oldsecret")
            client = authenticate_suspended("127.0.0.1", client_port, nick, "oldsecret")
            clients.append(client)
            start = len(client.lines)
            services.send_ins(f"N::{nick}::{field}", value)
            time.sleep(0.15)
            services.send_del(f"N::{nick}::suspend")
            client.receive(time.monotonic() + 0.6)
            events = client.lines[start:]
            require(not any(f" MODE {nick} " in line and "+r" in line for line in events),
                    f"{field} invalidation transiently restored +r: {events!r}")
            if not any(f" NICK :Guest" in line for line in events):
                assert_unidentified(client, nick, f"{field} invalidation")

        print("PASS: N::suspend retains only policy-bound local auth and restores it safely after DEL")
    finally:
        for client in clients:
            client.close()
        if services:
            services.close()
        if process:
            stop(process)
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
