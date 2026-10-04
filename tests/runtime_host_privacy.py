#!/usr/bin/env python3
"""Public-host privacy and authenticated -t/+t regression in an isolated IRCd."""

import argparse
import pathlib
import re
import shutil
import subprocess
import tempfile
import time

from runtime_channel_modes_ins import (
    DEFAULT_IRCD, EnvironmentUnavailable, FakeServices, IRCD_SID, IrcClient,
    LINK_PASSWORD, SERVICES_NAME, SERVICES_SID, bwrap_command, find_module_path,
    free_port, require, run_configtest, sha256, skip, stop, wait_for_daemon,
    write_config,
)
from udb_state_seed import seed_block, seed_ready_state
from runtime_nick_auth import build_state_module


def whois(client, nick):
    lines = client.request(f"WHOIS {nick}", lambda s: " 318 " in s, "WHOIS end")
    return next(s.split()[5] for s in lines if " 311 " in s)


def modes(client, nick):
    lines = client.request(f"MODE {nick}", lambda s: " 221 " in s, "user modes")
    return next(s.split()[3].lstrip("+") for s in lines if " 221 " in s)


def barrier(client):
    client.request("PING :privacy-barrier", lambda s: " PONG " in s, "command barrier")


def exercise(port, server_port, case, config):
    clients = []
    services = FakeServices("127.0.0.1", server_port, SERVICES_NAME, SERVICES_SID,
                            LINK_PASSWORD, IRCD_SID)
    try:
        services.wait_hel()
        services.hel_ack()
        for nick in ("NickServ", "ChanServ", "IpServ"):
            services.send_uid(nick)

        def service_barrier():
            start = len(services.lines)
            services.send("PING :services-barrier")
            services.wait_for(lambda s: " PONG " in s, "services command barrier", start=start)

        def connect(nick):
            c = IrcClient("127.0.0.1", port, nick)
            clients.append(c)
            services.send(f"SVS2NOLAG + {nick}")
            c.wait_for(lambda s: "exempted from fake lag" in s, "fake-lag exemption")
            return c

        observer = connect("observer")
        observer.request("JOIN #privacy", lambda s: " 366 " in s, "observer join")
        departing = connect("departing")
        departing.request("JOIN #privacy", lambda s: " 366 " in s, "subject join")
        host = whois(observer, "departing")
        require(re.fullmatch(r"[0-9a-f]{32}\.virtual", host), f"bad derived host: {host}")
        start = len(observer.lines)
        departing.close()
        clients.remove(departing)
        lines = observer.wait_for(lambda s: s.startswith(":departing!") and " QUIT " in s,
                                  "subject QUIT", start=start)
        quit_line = next(s for s in lines if s.startswith(":departing!") and " QUIT " in s)
        require(f"@{host} QUIT " in quit_line, f"QUIT changed protected host: {quit_line}")
        print("PASS: QUIT retains the visible protected host")
        if case == "quit":
            return

        require("x" in modes(observer, "observer") and "t" not in modes(observer, "observer"),
                "derived base must use +x without +t")
        base = whois(observer, "observer")
        observer.send("MODE observer -x")
        barrier(observer)
        require("x" in modes(observer, "observer") and whois(observer, "observer") == base,
                "-x removed privacy")
        observer.send("UMODE2 -x")
        barrier(observer)
        require("x" in modes(observer, "observer"), "UMODE2 bypassed privacy")
        services.send("SVS2MODE observer +r")
        observer.wait_for(lambda s: " MODE observer " in s and "+r" in s, "external +r")
        observer.send("MODE observer +t")
        barrier(observer)
        require("t" not in modes(observer, "observer") and whois(observer, "observer") == base,
                "external +r authorized a vhost without UDB identity")

        alice = connect("alice-setup")
        alice.request("NICK alice:secret", lambda s: " NICK :alice" in s, "identify alice")
        alice.wait_for(lambda s: " MODE alice " in s and "+r" in s, "UDB +r")
        require(whois(observer, "alice") == "alice.test", "N vhost not applied")
        alice.send("MODE alice -t")
        barrier(alice)
        require(whois(observer, "alice") == base, "-t did not return to UDB base cloak")
        require("x" in modes(alice, "alice") and "r" in modes(alice, "alice") and
                "t" not in modes(alice, "alice"), "-t lost privacy or identity")
        alice.send("MODE alice +t")
        barrier(alice)
        require(whois(observer, "alice") == "alice.test" and "t" in modes(alice, "alice"),
                "+t did not recover authorized N vhost")
        alice.send("UMODE2 +t-t")
        barrier(alice)
        require(whois(observer, "alice") == base and "t" not in modes(alice, "alice"),
                "mixed +t-t did not end on base")
        alice.send("UMODE2 -t+t")
        barrier(alice)
        require(whois(observer, "alice") == "alice.test", "mixed -t+t did not recover vhost")
        start = len(alice.lines)
        services.send("UDBTEST GETHOST alice")
        lines = alice.wait_for(lambda s: "UDBTEST REALHOST=" in s, "internal host state", start=start)
        state = next(s for s in lines if "UDBTEST REALHOST=" in s)
        require("REALHOST=localhost IP=127.0.0.1" in state and f"CLOAK={base}" in state,
                f"original identity/base overwritten: {state}")

        services.send("SVSMODE alice -x")
        services.send("SVS2MODE alice -xt")
        service_barrier()
        require("x" in modes(alice, "alice") and whois(observer, "alice") == "alice.test",
                "services bypassed permanent +x")

        # Early metadata supplies the base of a remote custom-hosted client.
        uid = f"{SERVICES_SID}999999"
        services.sock.sendall((f"@s2s-md/udb_base_cloak=remote-base.virtual :{SERVICES_SID} "
            f"UID remote-test 1 {int(time.time())} remote real.remote.test {uid} * +xt "
            "custom.remote.test * * :remote test\r\n").encode("ascii"))
        services.send(f"MD client {uid} udb_base_cloak :real.remote.test")
        services.send(f"MD client {uid} udb_base_cloak")
        services.send(f":{uid} UMODE2 -t")
        service_barrier()
        remote_host = whois(observer, "remote-test")
        require(remote_host == "remote-base.virtual",
                f"remote -t ignored early base or invalid MD erased it: {remote_host}")

        services.send_ins("I::127.0.0.1::host", "explicit.test")
        observer.wait_for(lambda s: "explicit IP vhost is now explicit.test" in s, "I overlay")
        start = len(observer.lines)
        services.send("UDBTEST GETHOST observer")
        lines = observer.wait_for(lambda s: "UDBTEST REALHOST=" in s, "I internal hosts", start=start)
        require(any("REALHOST=localhost IP=127.0.0.1" in s and f"CLOAK={base}" in s
                    for s in lines), "I override overwrote original identity/base")
        services.send_del("I::127.0.0.1::host")
        service_barrier()
        require(whois(observer, "observer") == base, "I removal lost base")

        operator = connect("privacy-oper")
        operator.request("OPER privacy secret", lambda s: " 381 " in s, "OPER")
        hel_start = len(services.lines)
        operator.request("REHASH", lambda s: " 382 " in s, "REHASH")
        services.wait_for(lambda s: " DB " in s and " HEL 4 " in s, "reload HEL", start=hel_start)
        services.seq = 0
        services.hel_ack()
        barrier(operator)
        require(whois(observer, "alice") == "alice.test" and "x" in modes(alice, "alice"),
                "reload lost the custom vhost or privacy")
        alice.send("MODE alice -t")
        barrier(alice)
        require(whois(observer, "alice") == base, "reload lost the base cloak")
        alice.send("MODE alice +t")
        barrier(alice)
        require(whois(observer, "alice") == "alice.test", "reload lost authenticated recovery")

        services.send_ins("S::suffix", ".changed.virtual")
        observer.wait_for(lambda s: "Your base cloak is now active" in s, "base rotation")
        changed = whois(observer, "observer")
        require(changed == base.removesuffix(".virtual") + ".changed.virtual", "incorrect base rotation")
        require(whois(observer, "alice") == "alice.test", "rotation replaced a custom host")
        alice.send("MODE alice -t")
        barrier(alice)
        require(whois(observer, "alice") == changed, "-t used stale base after rotation")
        services.send_del("S::suffix")
        service_barrier()
        fresh = connect("native-fallback")
        require(whois(observer, "observer") == changed, "setting removal erased existing base")
        native = whois(observer, "native-fallback")
        require(native not in ("localhost", "127.0.0.1") and "x" in modes(fresh, "native-fallback") and
                "t" not in modes(fresh, "native-fallback"), "missing settings lost native privacy")

        services.send_ins("N::alice::suspend", "privacy regression")
        alice.wait_for(lambda s: "nickname is suspended" in s, "suspend")
        services.send("SVS2MODE alice +r")
        service_barrier()
        alice.send("MODE alice +t")
        barrier(alice)
        require(whois(observer, "alice") == changed and "t" not in modes(alice, "alice"),
                "residual +r recovered a suspended/unidentified vhost")

        # Refuse hot removal, leaving the running module and its guards intact.
        config.write_text(config.read_text().replace('loadmodule "third/udb";', ''))
        operator.request("REHASH", lambda s: " 382 " in s, "unload attempt")
        barrier(operator)
        observer.send("MODE observer -x")
        barrier(observer)
        require("x" in modes(observer, "observer"), "hot unload disabled privacy")
        print("PASS: permanent +x, derived base and authenticated -t/+t")
        print("PASS: original host/IP, remote metadata, reload, rotation, fallback and revocation")
    finally:
        for c in clients:
            c.close()
        services.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ircd", type=pathlib.Path, default=DEFAULT_IRCD)
    parser.add_argument("--module", type=pathlib.Path, default=find_module_path())
    parser.add_argument("--case", choices=("quit", "all"), default="all")
    args = parser.parse_args()
    if not shutil.which("bwrap") or not args.ircd.is_file() or not args.module.is_file():
        return skip("bwrap, installed IRCd and compiled module are required")
    process = None
    with tempfile.TemporaryDirectory(prefix="udb-host-privacy-") as temp:
        node = pathlib.Path(temp)
        data = node / "runtime-data"
        data.mkdir()
        third = node / "modules" / "third"
        third.mkdir(parents=True)
        shutil.copy2(args.module, third / "udb.so")
        shutil.copy2(build_state_module(), third / "udb_test_state.so")
        seed_block(data / "udb_N.db", "N", f"alice::pass sha256:{sha256('secret')}\n"
                   "alice::access 127.0.0.0/8\nalice::vhost alice.test\n")
        seed_block(data / "udb_S.db", "S", "encryption_key " + "a1" * 32 + "\nsuffix .virtual\n")
        for block in "CILK":
            seed_block(data / f"udb_{block}.db", block)
        seed_ready_state(data)
        port, server_port, tls_port = free_port(), free_port(), free_port()
        config = node / "unrealircd.conf"
        write_config(config, "privacy.test", IRCD_SID, port, server_port, tls_port, args.module, data)
        # Exercise several signed-mode transitions without exhausting the stock
        # vhost flood budget; the production module still checks that budget.
        config.write_text(config.read_text() +
                          '\nset { anti-flood { known-users { vhost-flood 100:60; } '
                          'unknown-users { vhost-flood 100:60; } } }\n'
                          'include "' + str(DEFAULT_IRCD.parents[1] / "conf/operclass.default.conf") + '";\n'
                          'oper privacy { mask "*@*"; password "secret"; operclass netadmin; class clients; }\n'
                          'loadmodule "third/udb_test_state";\n')
        try:
            run_configtest(node, args.ircd, config)
            with (node / "ircd.log").open("w") as log:
                process = subprocess.Popen(bwrap_command(node, args.ircd, config), stdout=log,
                                           stderr=subprocess.STDOUT)
            wait_for_daemon(process, (("127.0.0.1", port),), 15)
            exercise(port, server_port, args.case, config)
        except EnvironmentUnavailable as exc:
            return skip(str(exc))
        except (AssertionError, OSError, RuntimeError, subprocess.SubprocessError) as exc:
            print(f"FAIL: {exc}")
            if (node / "ircd.log").exists():
                print((node / "ircd.log").read_text(errors="replace")[-3000:])
            return 1
        finally:
            stop(process)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
