#!/usr/bin/env python3
"""Protected custom/base hosts across A-B-C, including a late C join."""
import pathlib
import shutil
import subprocess
import tempfile

from runtime_host_privacy import whois
from runtime_channel_modes_ins import IrcClient, require, sha256, wait_for_daemon
from test_ocl_membership_multihop import (DEFAULT_IRCD, RUNTIME_ROOT, free_ports,
                                        write_config, bwrap_command, stop, wait_until)
from udb_state_seed import seed_block, seed_ready_state


def barrier(client):
    start = len(client.lines)
    client.send('PING :privacy-barrier')
    client.wait_for(lambda s: ' PONG ' in s, 'command barrier', start=start, timeout=15)


def main():
    processes, clients = [], []
    with tempfile.TemporaryDirectory(prefix="udb-privacy-multihop-") as temp:
        root = pathlib.Path(temp)
        ports = {n: tuple(free_ports(3)) for n in "ABC"}
        names = {n: f"privacy-{n.lower()}.test" for n in "ABC"}
        nodes = {n: root / n for n in "ABC"}
        try:
            for n in "ABC":
                node = nodes[n]
                data = node / "runtime-data"
                data.mkdir(parents=True)
                third = node / "modules/third"
                third.mkdir(parents=True)
                shutil.copy2(RUNTIME_ROOT / "modules/third/udb.so", third / "udb.so")
                seed_block(data / "udb_N.db", "N", f"alice::pass sha256:{sha256('secret')}\n"
                           "alice::access 127.0.0.0/8\nalice::vhost alice.test\n")
                seed_block(data / "udb_S.db", "S", "encryption_key " + "a1" * 32 + "\nsuffix .virtual\n")
                for block in "CILK":
                    seed_block(data / f"udb_{block}.db", block)
                seed_ready_state(data)
                links = ([(names['B'], ports['B'][1], True)] if n == 'A' else
                         [(names['A'], ports['A'][1], False), (names['C'], ports['C'][1], True)]
                         if n == 'B' else [(names['B'], ports['B'][1], False)])
                write_config(node / "unrealircd.conf", names[n], f"0{n}1", ports[n], links, data)
                config = node / "unrealircd.conf"
                config.write_text(config.read_text() +
                    '\nset { anti-flood { known-users { vhost-flood 100:60; } '
                    'unknown-users { vhost-flood 100:60; } } }\n')

            def start(n):
                node = nodes[n]
                with (node / 'ircd.log').open('w') as log:
                    process = subprocess.Popen(bwrap_command(node, DEFAULT_IRCD, node / 'unrealircd.conf'),
                                               stdout=log, stderr=subprocess.STDOUT)
                processes.append(process)
                wait_for_daemon(process, (("127.0.0.1", ports[n][0]),), 15)

            start('B')
            start('A')
            wait_until(lambda: names['A'] in (nodes['B'] / 'ircd.log').read_text(), 'A-B link')
            alice = IrcClient('127.0.0.1', ports['A'][0], 'alice-setup')
            clients.append(alice)
            base = whois(alice, 'alice-setup')
            alice.request('NICK alice:secret', lambda s: ' NICK :alice' in s, 'identify')
            alice.wait_for(lambda s: ' MODE alice ' in s and '+r' in s, 'identity')
            require(whois(alice, 'alice') == 'alice.test', 'custom host before late join')
            opted = IrcClient('127.0.0.1', ports['A'][0], 'open-user')
            clients.append(opted)
            opted.send('MODE open-user -x')
            barrier(opted)
            start('C')
            wait_until(lambda: names['C'] in (nodes['B'] / 'ircd.log').read_text(), 'late B-C link')
            observer = IrcClient('127.0.0.1', ports['C'][0], 'observer')
            clients.append(observer)
            # WHOIS completion acts as a state probe; server linking may still be bursting.
            def visible():
                lines = observer.request('WHOIS alice', lambda s: ' 318 ' in s, 'remote WHOIS')
                return any(' 311 ' in s for s in lines)
            wait_until(visible, 'late remote client')
            require(whois(observer, 'alice') == 'alice.test', 'late burst lost custom host')
            require(whois(observer, 'open-user') == 'localhost', 'late burst cancelled voluntary -x')
            opted.send('MODE open-user +x')
            barrier(opted)
            require(whois(observer, 'open-user') == base, 'late burst lost opted-out base')
            observer.request('JOIN #privacy', lambda s: ' 366 ' in s, 'remote join')
            alice.request('JOIN #privacy', lambda s: ' 366 ' in s, 'origin join')
            observer.wait_for(lambda s: s.startswith(':alice!') and ' JOIN ' in s, 'custom JOIN')
            alice.send('MODE alice -t')
            barrier(alice)
            require(whois(observer, 'alice') == base, 'multihop -t used native instead of origin base')
            alice.send('MODE alice +t')
            barrier(alice)
            require(whois(observer, 'alice') == 'alice.test', 'multihop +t failed')
            alice.send('MODE alice -x')
            barrier(alice)
            require(whois(observer, 'alice') == 'localhost', 'multihop voluntary -x rejected')
            alice.send('MODE alice +x')
            barrier(alice)
            require(whois(observer, 'alice') == base, 'multihop +x lost origin base')
            alice.send('MODE alice +t')
            barrier(alice)
            require(whois(observer, 'alice') == 'alice.test', 'multihop +t after +x failed')
            start_line = len(observer.lines)
            alice.close()
            clients.remove(alice)
            lines = observer.wait_for(lambda s: s.startswith(':alice!') and ' QUIT ' in s,
                                      'remote QUIT', start=start_line)
            require(any('@alice.test QUIT ' in s for s in lines), f'remote QUIT disclosure: {lines}')
            print('PASS: A-B-C late burst, custom/base -t/+t, voluntary -x/+x and protected QUIT')
        except (AssertionError, OSError, RuntimeError, subprocess.SubprocessError) as exc:
            print(f'FAIL: {exc}')
            for n in 'ABC':
                log = nodes[n] / 'ircd.log'
                if log.exists():
                    print(n, log.read_text(errors='replace')[-1500:])
            return 1
        finally:
            for c in clients:
                c.close()
            for p in processes:
                stop(p)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
