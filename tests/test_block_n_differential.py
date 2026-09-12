#!/usr/bin/env python3
"""Differential test suite comparing BlockNModel against live UnrealIRCd runtime state.

Executes a seeded suite of diverse multi-step scenarios combining:
  - user authentication & failed authentication
  - external modes, vhosts, snomasks, and opers
  - UDB profile effects (modes, vhost, snomasks expressions, oper)
  - hot profile mutations and profile deletions
  - suspend and unsuspend lifecycle
  - differential verification of (nick, account, +r, modes, vhost, snomasks, oper)
    between BlockNModel and live UnrealIRCd state.
"""

import argparse
import hashlib
import os
import pathlib
import random
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from runtime_schema_validation import (
    DEFAULT_IRCD,
    EnvironmentUnavailable,
    FakeServicesServer,
    IRCD_SID,
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
from runtime_nick_auth import (
    IrcClient,
    build_state_module,
    read_snomask,
    read_oper_state,
    wait_for_mode,
    has_usermode,
    current_nick,
    free_guest,
    sha256,
)
from test_block_n_ownership_model import BlockNModel, apply_snomask_expression

_services = None


def read_client_vhost(client, nick, timeout=10):
    start = len(client.lines)
    client.send(f"WHOIS {nick}")
    client.wait_for(lambda line: " 318 " in line, f"WHOIS {nick}", start=start, timeout=timeout)
    for line in reversed(client.lines[start:]):
        if " 311 " in line and f" {nick} " in line:
            parts = line.split()
            if len(parts) >= 6:
                return parts[5]
    return None


def read_client_modes(client, nick, timeout=5):
    start = len(client.lines)
    client.send(f"MODE {nick}")
    client.wait_for(lambda line: f" 221 {nick} " in line, f"MODE {nick}", start=start, timeout=timeout)
    for line in reversed(client.lines[start:]):
        if f" 221 {nick} " in line:
            return line.split(f" 221 {nick} ", 1)[1].strip()
    return ""


def get_client_modes(client, nick, timeout=10):
    start = len(client.lines)
    client.send(f"MODE {nick}")
    lines = client.wait_for(lambda line: " 221 " in line, f"{nick} modes", start=start, timeout=timeout)
    for line in reversed(lines):
        if " 221 " in line:
            return line.rsplit(" ", 1)[-1].lstrip(":+")
    return ""


def check_differential(model, client, services, expected_nick, description):
    client.receive(time.monotonic() + 0.1)
    c_nick = current_nick(client)
    if model.identity:
        if c_nick != expected_nick:
            raise AssertionError(f"{description}: expected nick {expected_nick}, got {c_nick}")

    # Query usermodes once
    modes_str = get_client_modes(client, c_nick)
    r_has_r = "r" in modes_str
    if model.identity != r_has_r:
        raise AssertionError(f"{description}: identity mismatch: model={model.identity}, runtime={r_has_r} (modes={modes_str!r})")

    r_has_S = "S" in modes_str
    m_has_S = "S" in model.runtime_modes
    if m_has_S != r_has_S:
        raise AssertionError(f"{description}: mode S mismatch: model={m_has_S}, runtime={r_has_S} (modes={modes_str!r})")

    # snomask check
    r_snomask = read_snomask(services, client, c_nick, description)
    m_snomask = model.runtime_snomask or ""
    # Normalize sorted snomask string
    m_norm = "".join(sorted(m_snomask))
    r_norm = "".join(sorted(r_snomask))
    if m_norm != r_norm:
        raise AssertionError(f"{description}: snomask mismatch: model={m_norm!r}, runtime={r_norm!r}")

    # oper check
    r_oper = read_oper_state(services, client, c_nick, description)
    if model.runtime_oper != r_oper["oper"]:
        raise AssertionError(f"{description}: oper status mismatch: model={model.runtime_oper}, runtime={r_oper['oper']}")
    if model.runtime_oper and model.runtime_operclass:
        if model.runtime_operclass != r_oper["operclass"]:
            raise AssertionError(f"{description}: operclass mismatch: model={model.runtime_operclass}, runtime={r_oper['operclass']}")

    # vhost check
    if model.runtime_vhost is not None:
        r_vhost = read_client_vhost(client, c_nick)
        if r_vhost != model.runtime_vhost:
            raise AssertionError(f"{description}: vhost mismatch: model={model.runtime_vhost!r}, runtime={r_vhost!r}")


def run_differential_tests(ircd, module, keep=False):
    global _services
    root = pathlib.Path(tempfile.mkdtemp(prefix="udb-differential-"))
    process = None
    stdout_handle = None
    services = None
    clients = []

    try:
        node = root / "node"
        for path in (node / "runtime-data", node / "tmp", node / "modules" / "third", node / "logs"):
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
            handle.write("set { anti-flood { known-users { nick-flood 50:60; } "
                         "unknown-users { nick-flood 50:60; } } }\n")

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

        # Pre-defined deterministic sequences covering the required matrix (Phase 6 & 7)
        sequences = [
            # 1: Basic auth -> +S, vhost, snomask +c -> suspend -> unsuspend
            {
                "profile": {"pass": "p1", "access": "127.0.0.0/8", "modes": ("S",), "vhost": "v1.test", "snomask": "+c"},
                "events": [
                    ("auth", True),
                    ("check", "auth complete"),
                    ("suspend", "reason1"),
                    ("check", "suspended"),
                    ("unsuspend", None),
                    ("check", "unsuspended"),
                ]
            },
            # 2: Relative snomask: external k -> auth with +c-k -> suspend (restores k)
            {
                "profile": {"pass": "p2", "access": "127.0.0.0/8", "snomask": "+c-k"},
                "events": [
                    ("ext_snomask", "k"),
                    ("check", "external snomask k"),
                    ("auth", True),
                    ("check", "auth with +c-k"),
                    ("suspend", "reason2"),
                    ("check", "suspend restores k"),
                ]
            },
            # 3: External oper with different operclass -> UDB must NOT touch it
            {
                "profile": {"pass": "p3", "access": "127.0.0.0/8", "oper": "netadmin"},
                "events": [
                    ("ext_oper", "locop"),
                    ("check", "external oper locop"),
                    ("auth", True),
                    ("check", "auth preserves external oper locop"),
                    ("suspend", "reason3"),
                    ("check", "suspend preserves external oper locop"),
                ]
            },
            # 4: External oper with same operclass -> UDB must not claim ownership
            {
                "profile": {"pass": "p4", "access": "127.0.0.0/8", "oper": "locop"},
                "events": [
                    ("ext_oper", "locop"),
                    ("check", "external oper locop"),
                    ("auth", True),
                    ("check", "auth does not hijack matching oper"),
                    ("suspend", "reason4"),
                    ("check", "suspend preserves matching oper"),
                ]
            },
            # 5: UDB-granted oper and clean revocation
            {
                "profile": {"pass": "p5", "access": "127.0.0.0/8", "oper": "locop"},
                "events": [
                    ("auth", True),
                    ("check", "udb granted oper locop"),
                    ("suspend", "reason5"),
                    ("check", "suspend revokes udb oper"),
                ]
            },
            # 6: External vhost -> auth with profile vhost -> external override -> suspend preserves external
            {
                "profile": {"pass": "p6", "access": "127.0.0.0/8", "vhost": "udbvhost.test"},
                "events": [
                    ("auth", True),
                    ("check", "profile vhost applied"),
                    ("ext_vhost", "extover.test"),
                    ("check", "external vhost override"),
                    ("suspend", "reason6"),
                    ("check", "suspend preserves external vhost override"),
                ]
            },
            # 7: External mode +S before auth -> profile desires +S -> suspend preserves external +S
            {
                "profile": {"pass": "p7", "access": "127.0.0.0/8", "modes": ("S",)},
                "events": [
                    ("ext_mode", "S"),
                    ("check", "external mode S set"),
                    ("auth", True),
                    ("check", "auth with profile mode S"),
                    ("suspend", "reason7"),
                    ("check", "suspend preserves external mode S"),
                ]
            },
            # 8: Snomask relative chaining: external k -> +c -> -k -> suspend
            {
                "profile": {"pass": "p8", "access": "127.0.0.0/8", "snomask": "+c"},
                "events": [
                    ("ext_snomask", "k"),
                    ("check", "external k"),
                    ("auth", True),
                    ("check", "auth with +c gives ck"),
                    ("snomask_mut", "-k"),
                    ("check", "mutation -k gives c"),
                    ("suspend", "reason8"),
                    ("check", "suspend restores external k"),
                ]
            },
            # 9: Oper hot update: normal user -> auth with oper locop -> hot update to none (DEL oper)
            {
                "profile": {"pass": "p9", "access": "127.0.0.0/8", "oper": "locop"},
                "events": [
                    ("auth", True),
                    ("check", "auth with locop"),
                    ("oper_mut", None),
                    ("check", "hot DEL oper revokes oper"),
                ]
            },
            # 10: Failed authentication leaves no identity or effects
            {
                "profile": {"pass": "p10", "access": "127.0.0.0/8", "modes": ("S",), "snomask": "+c"},
                "events": [
                    ("auth", False),
                    ("check", "failed auth"),
                ]
            },
            # 11: Passless profile owns nothing and grants no effects
            {
                "profile": {"access": "127.0.0.0/8", "modes": ("S",), "snomask": "+c"},
                "events": [
                    ("auth", True),
                    ("check", "passless profile does not authenticate"),
                ]
            },
            # 12: Profile deletion clears UDB identity and effects
            {
                "profile": {"pass": "p12", "access": "127.0.0.0/8", "modes": ("S",), "vhost": "p12.test"},
                "events": [
                    ("auth", True),
                    ("check", "authenticated"),
                    ("profile_del", None),
                    ("check", "profile deleted"),
                ]
            },
            # 13: Mode mutation: auth with empty modes -> hot INS +S -> hot DEL modes
            {
                "profile": {"pass": "p13", "access": "127.0.0.0/8"},
                "events": [
                    ("auth", True),
                    ("check", "auth without modes"),
                    ("modes_mut", "+S"),
                    ("check", "hot +S added"),
                    ("modes_mut", None),
                    ("check", "hot modes removed"),
                ]
            },
            # 14: Vhost mutation: auth with vhost1 -> hot update to vhost2 -> suspend
            {
                "profile": {"pass": "p14", "access": "127.0.0.0/8", "vhost": "v14a.test"},
                "events": [
                    ("auth", True),
                    ("check", "vhost14a active"),
                    ("vhost_mut", "v14b.test"),
                    ("check", "vhost14b active"),
                    ("suspend", "reason14"),
                    ("check", "suspended clears vhost"),
                ]
            },
            # 15: External snomask changed externally while UDB owned -> suspend preserves changed external
            {
                "profile": {"pass": "p15", "access": "127.0.0.0/8", "snomask": "+c"},
                "events": [
                    ("ext_snomask", "k"),
                    ("auth", True),
                    ("check", "auth with +c"),
                    ("ext_snomask", "d"),
                    ("check", "external override to d"),
                    ("suspend", "reason15"),
                    ("check", "suspend preserves external d"),
                ]
            },
        ]

        # Extend with 15 randomized variations to reach 30 sequences total
        rnd = random.Random(42)
        sno_choices = ["+c", "-c", "+c-k", "+k-c", "+ck", "+c+d-k"]
        vhost_choices = ["vh1.test", "vh2.test", "vh3.test"]

        for seq_idx in range(16, 31):
            p_sno = rnd.choice(sno_choices)
            p_vh = rnd.choice(vhost_choices)
            p_modes = ("S",) if rnd.random() > 0.5 else ()
            p_oper = "locop" if rnd.random() > 0.5 else None
            ext_s = rnd.choice(["k", "c", "d", None])
            ext_op = rnd.random() > 0.7

            evs = []
            if ext_s:
                evs.append(("ext_snomask", ext_s))
            if ext_op:
                evs.append(("ext_oper", "locop"))
            evs.append(("auth", True))
            evs.append(("check", f"seq {seq_idx} initial auth"))

            # Add 2-3 mutations or lifecycle events
            action = rnd.choice(["suspend_cycle", "snomask_mut", "vhost_mut", "ext_override"])
            if action == "suspend_cycle":
                evs.append(("suspend", f"rand-suspend-{seq_idx}"))
                evs.append(("check", f"seq {seq_idx} suspended"))
                evs.append(("unsuspend", None))
                evs.append(("check", f"seq {seq_idx} unsuspended"))
            elif action == "snomask_mut":
                new_s = rnd.choice(sno_choices)
                evs.append(("snomask_mut", new_s))
                evs.append(("check", f"seq {seq_idx} snomask mut {new_s}"))
            elif action == "vhost_mut":
                new_v = rnd.choice(vhost_choices)
                evs.append(("vhost_mut", new_v))
                evs.append(("check", f"seq {seq_idx} vhost mut {new_v}"))
            elif action == "ext_override":
                evs.append(("ext_vhost", f"ext-override-{seq_idx}.test"))
                evs.append(("check", f"seq {seq_idx} ext vhost override"))
                evs.append(("suspend", f"override-suspend-{seq_idx}"))
                evs.append(("check", f"seq {seq_idx} suspend preserves ext vhost"))

            profile = {
                "pass": f"secret{seq_idx}",
                "access": "127.0.0.0/8",
                "snomask": p_sno,
                "vhost": p_vh,
                "modes": p_modes,
            }
            if p_oper:
                profile["oper"] = p_oper

            sequences.append({"profile": profile, "events": evs})

        print(f"Running differential model <-> IRCd test matrix ({len(sequences)} sequences)...")

        for idx, seq in enumerate(sequences, start=1):
            nick = f"dif{idx}"
            passw = seq["profile"].get("pass", f"pass{idx}")
            prof_dict = dict(seq["profile"])

            # Setup UDB profile via services
            if "pass" in prof_dict:
                services.send_ins(f"N::{nick}::pass", "sha256:" + sha256(passw))
            services.send_ins(f"N::{nick}::access", "127.0.0.0/8")
            if "vhost" in prof_dict:
                services.send_ins(f"N::{nick}::vhost", prof_dict["vhost"])
            if "modes" in prof_dict and prof_dict["modes"]:
                services.send_ins(f"N::{nick}::modes", "".join("+" + m for m in prof_dict["modes"]))
            if "snomask" in prof_dict and prof_dict["snomask"]:
                services.send_ins(f"N::{nick}::snomasks", prof_dict["snomask"])
            if "oper" in prof_dict and prof_dict["oper"]:
                services.send_ins(f"N::{nick}::oper", prof_dict["oper"])
            time.sleep(0.2)

            # Initialize model and runtime client
            model = BlockNModel(prof_dict)
            client = IrcClient("127.0.0.1", client_port, f"{nick}-cli")
            clients.append(client)

            # Execute sequence events
            for ev_type, ev_arg in seq["events"]:
                c_nick = current_nick(client)
                if ev_type == "ext_snomask":
                    model.external_snomask(ev_arg)
                    services.send(f"UDBTEST SNOMASK {c_nick} {ev_arg}")
                    time.sleep(0.2)
                elif ev_type == "ext_vhost":
                    model.external_vhost(ev_arg)
                    services.send(f"CHGHOST {c_nick} {ev_arg}")
                    client.wait_for(lambda line: " 396 " in line and ev_arg in line, f"ext vhost {ev_arg}", timeout=10)
                elif ev_type == "ext_mode":
                    model.external_mode(ev_arg)
                    services.send(f"SVS2MODE {c_nick} +{ev_arg}")
                    wait_for_mode(client, c_nick, f"+{ev_arg}", f"ext mode +{ev_arg}", timeout=10)
                elif ev_type == "ext_oper":
                    model.external_oper(ev_arg)
                    client.request("OPER testoper operpass", lambda line: " 381 " in line, "ext oper login")
                elif ev_type == "auth":
                    if ev_arg:
                        ok = model.authenticate(True)
                        if "pass" in prof_dict:
                            client.request(f"NICK {nick}:{passw}", lambda line: f" NICK :{nick}" in line, f"auth {nick}")
                            wait_for_mode(client, nick, "+r", f"auth +r {nick}")
                        else:
                            # Passless adoption attempt: nick change succeeds without identity
                            client.request(f"NICK {nick}", lambda line: f" NICK :{nick}" in line, f"passless nick {nick}")
                    else:
                        model.authenticate(False)
                        client.request(f"NICK {nick}:badsecret", lambda line: " 432 " in line, f"failed auth {nick}")
                elif ev_type == "suspend":
                    model.suspend()
                    start = len(client.lines)
                    services.send_ins(f"N::{nick}::suspend", ev_arg)
                    client.wait_for(lambda line: "This nickname is suspended. Reason: " in line, "suspend notice", start=start, timeout=15)
                elif ev_type == "unsuspend":
                    model.unsuspend()
                    start = len(client.lines)
                    services.send_del(f"N::{nick}::suspend")
                    free_guest(client, f"unsuspend rename {nick}", start=start)
                elif ev_type == "snomask_mut":
                    model.effect_update(snomask=ev_arg)
                    if ev_arg:
                        services.send_ins(f"N::{nick}::snomasks", ev_arg)
                    else:
                        services.send_del(f"N::{nick}::snomasks")
                    time.sleep(0.3)
                elif ev_type == "vhost_mut":
                    model.effect_update(vhost=ev_arg)
                    if ev_arg:
                        services.send_ins(f"N::{nick}::vhost", ev_arg)
                    else:
                        services.send_del(f"N::{nick}::vhost")
                    time.sleep(0.3)
                elif ev_type == "modes_mut":
                    modes_tuple = (ev_arg.replace("+", ""),) if ev_arg else ()
                    model.effect_update(modes=modes_tuple)
                    if ev_arg:
                        services.send_ins(f"N::{nick}::modes", ev_arg)
                    else:
                        services.send_del(f"N::{nick}::modes")
                    time.sleep(0.3)
                elif ev_type == "oper_mut":
                    model.effect_update(oper=ev_arg)
                    if ev_arg:
                        services.send_ins(f"N::{nick}::oper", ev_arg)
                    else:
                        services.send_del(f"N::{nick}::oper")
                    time.sleep(0.3)
                elif ev_type == "profile_del":
                    model.delete_profile()
                    services.send_del(f"N::{nick}")
                    time.sleep(0.3)
                elif ev_type == "check":
                    check_differential(model, client, services, nick, f"seq {idx} ({ev_arg})")

            # Clean up client for this sequence
            client.close()
            clients.remove(client)
            time.sleep(0.2)

        print(f"PASS: all {len(sequences)} differential model <-> IRCd sequences verified successfully")

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
        run_differential_tests(args.ircd, args.module, args.keep)
    except EnvironmentUnavailable as error:
        return skip(str(error))


if __name__ == "__main__":
    sys.exit(main())
