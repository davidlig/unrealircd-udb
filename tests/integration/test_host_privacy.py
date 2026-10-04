"""Current runtime contracts for UDB-derived host privacy and custom vhosts."""

import hashlib
import re

import pytest

from udb_test_support.peer import Peer
from udb_state_seed import seed_block, seed_ready_state

pytestmark = pytest.mark.integration


def seed_privacy_state(data_dir):
    digest = hashlib.sha256(b"secret").hexdigest()
    seed_block(data_dir / "udb_N.db", "N",
               f"alice::pass sha256:{digest}\n"
               "alice::access 127.0.0.0/8\n"
               "alice::vhost alice.test\n")
    seed_block(data_dir / "udb_S.db", "S",
               "encryption_key " + "a1" * 32 + "\nsuffix .virtual\n")
    for letter in "CILK":
        seed_block(data_dir / f"udb_{letter}.db", letter)
    seed_ready_state(data_dir)


def host(client, nick):
    replies = client.request(f"WHOIS {nick}", terminator=lambda line: " 318 " in line)
    line = next(line for line in replies if " 311 " in line)
    return line.split()[5]


def modes(client, nick):
    replies = client.request(f"MODE {nick}", terminator=lambda line: " 221 " in line)
    line = next(line for line in replies if " 221 " in line)
    return set(line.rsplit(" ", 1)[-1].lstrip(":+"))


def change_mode(client, command, nick, mode):
    start = len(client.lines)
    client.send(command)
    return client.wait(lambda line: f" MODE {nick} " in line and mode in line,
                       f"{command} mode event", start=start)


def identify(client):
    replies = client.request("NICK alice:secret")
    assert any(line.endswith(" NICK :alice") for line in replies), replies
    assert any("You are now identified for nickname" in line for line in replies), replies


def test_local_base_cloak_mode_opt_out_and_quit_host_are_consistent(node_factory):
    node = node_factory(ready=False, prepare=seed_privacy_state,
                        name="privacy.test", sid="0T1",
                        peers=["services.test", "untrusted.test"],
                        propagator="services.test", ulines=["services.test"],
                        runtime_state_fixture=True, vhost_flood_limit=100)
    with Peer(node, name="services.test", sid="0S1") as services, \
            Peer(node, name="untrusted.test", sid="0U1") as untrusted:
        observer = node.client("observer")
        base = host(observer, "observer")
        assert re.fullmatch(r"[0-9a-f]{32}\.virtual", base)
        assert "x" in modes(observer, "observer") and "t" not in modes(observer, "observer")

        observer.send("MODE observer -x")
        assert "x" not in modes(observer, "observer")
        assert host(observer, "observer") == "localhost"
        observer.send("MODE observer +x")
        assert "x" in modes(observer, "observer")
        assert host(observer, "observer") == base

        observer.send("UMODE2 -x")
        assert "x" not in modes(observer, "observer")
        assert host(observer, "observer") == "localhost"
        observer.send("UMODE2 +x")
        assert host(observer, "observer") == base

        departing = node.client("departing")
        departing_base = host(departing, "departing")
        observer.request("JOIN #privacy", terminator=lambda line: " 366 " in line)
        departing.request("JOIN #privacy", terminator=lambda line: " 366 " in line)
        start = len(observer.lines)
        departing.send("QUIT :privacy test")
        quit_lines = observer.wait(lambda line: line.startswith(":departing!") and " QUIT " in line,
                                   "protected QUIT host", start=start)
        assert f"@{departing_base} QUIT " in quit_lines

        # External +r alone is not UDB nickname identity and cannot enable +t.
        start = len(observer.lines)
        services.send("SVS2MODE observer +r")
        services.barrier()
        observer.send("MODE observer +t")
        assert "r" in modes(observer, "observer")
        assert "t" not in modes(observer, "observer")
        assert host(observer, "observer") == base


def test_authenticated_vhost_uses_base_cloak_for_minus_t_and_preserves_origin(node_factory):
    node = node_factory(ready=False, prepare=seed_privacy_state,
                        name="privacy.test", sid="0T1", peers=["services.test"],
                        propagator="services.test", ulines=["services.test"],
                        runtime_state_fixture=True, vhost_flood_limit=100)
    with Peer(node, name="services.test", sid="0S1") as services:
        observer = node.client("observer")
        observer_base = host(observer, "observer")
        client = node.client("alice-setup")
        base = host(client, "alice-setup")
        assert base == observer_base
        identify(client)
        assert host(observer, "alice") == "alice.test"
        assert "r" in modes(client, "alice")

        change_mode(client, "MODE alice -t", "alice", "-t")
        assert host(observer, "alice") == base
        assert {"x", "r"} <= modes(client, "alice") and "t" not in modes(client, "alice")
        change_mode(client, "MODE alice +t", "alice", "+t")
        assert host(observer, "alice") == "alice.test"
        client.request("UMODE2 +t-t")
        assert host(observer, "alice") == base
        client.request("UMODE2 -t+t")
        assert host(observer, "alice") == "alice.test"

        start = len(client.lines)
        services.send("UDBTEST GETHOST alice")
        state = client.wait(lambda line: "UDBTEST REALHOST=" in line,
                            "original host and IP", start=start)
        assert "REALHOST=localhost IP=127.0.0.1" in state
        assert f"CLOAK={base}" in state

        replies = client.request("MODE observer -x")
        assert any(" 502 " in line for line in replies), replies
        assert "x" in modes(observer, "observer")

        services.send("SVSMODE alice -x")
        services.barrier()
        assert "x" not in modes(client, "alice")
        assert host(observer, "alice") == "localhost"
        services.send("SVS2MODE alice -xt")
        services.barrier()
        assert "t" not in modes(client, "alice")
        services.send("SVS2MODE alice +x")
        services.barrier()
        assert host(observer, "alice") == base


def test_explicit_ip_overlay_and_remote_base_metadata_do_not_replace_real_identity(node_factory):
    node = node_factory(ready=False, prepare=seed_privacy_state,
                        name="privacy.test", sid="0T1", peers=["services.test"],
                        propagator="services.test", ulines=["services.test"],
                        runtime_state_fixture=True)
    with Peer(node, name="services.test", sid="0S1") as services:
        observer = node.client("observer")
        base = host(observer, "observer")
        assert "x" in modes(observer, "observer")

        services.mutation(1, "I::127.0.0.1::host", "explicit.test")
        assert host(observer, "observer") == "explicit.test"
        start = len(observer.lines)
        services.send("UDBTEST GETHOST observer")
        state = observer.wait(lambda line: "UDBTEST REALHOST=" in line,
                              "original identity during I overlay", start=start)
        assert "REALHOST=localhost IP=127.0.0.1" in state and f"CLOAK={base}" in state

        services.mutation(2, "I::127.0.0.1::host")
        assert host(observer, "observer") == base

        # The early base-cloak tag precedes UID; invalid updates must not replace it.
        uid = f"{services.sid}999999"
        services.wire.send(
            f"@s2s-md/udb_base_cloak=remote-base.virtual :{services.sid} "
            f"UID remote-test 1 1700000000 remote real.remote.test {uid} * +xt "
            "custom.remote.test * * :remote test"
        )
        services.send(f"MD client {uid} udb_base_cloak :real.remote.test")
        services.send(f"MD client {uid} udb_base_cloak")
        services.send(f":{uid} UMODE2 -t")
        services.barrier()
        assert host(observer, "remote-test") == "remote-base.virtual"


def test_suffix_refresh_rehash_and_setting_removal_preserve_live_host_ownership(node_factory):
    node = node_factory(ready=False, prepare=seed_privacy_state,
                        name="privacy.test", sid="0T1", peers=["services.test"],
                        propagator="services.test", ulines=["services.test"],
                        vhost_flood_limit=100)
    with Peer(node, name="services.test", sid="0S1") as services:
        observer = node.client("observer")
        original_base = host(observer, "observer")
        voluntary = node.client("voluntary")
        voluntary.send("MODE voluntary -x")
        assert host(voluntary, "voluntary") == "localhost"

        alice = node.client("alice-setup")
        identify(alice)
        assert host(observer, "alice") == "alice.test"
        services.mutation(1, "S::suffix", ".changed.virtual")
        changed_base = host(observer, "observer")
        assert changed_base == original_base.removesuffix(".virtual") + ".changed.virtual"
        assert host(voluntary, "voluntary") == "localhost"
        assert "x" not in modes(voluntary, "voluntary")
        change_mode(voluntary, "MODE voluntary +x", "voluntary", "+x")
        assert host(observer, "voluntary") == changed_base
        assert host(observer, "alice") == "alice.test"

        change_mode(alice, "MODE alice -t", "alice", "-t")
        assert host(observer, "alice") == changed_base
        change_mode(alice, "MODE alice +t", "alice", "+t")
        assert host(observer, "alice") == "alice.test"

        operator = node.client("privacy-oper")
        assert any(" 381 " in line for line in operator.request("OPER testoper operpass"))
        start = len(services.lines)
        assert operator.request("REHASH", terminator=lambda line: " 382 " in line)
        services.wait(lambda line: " DB " in line and " HEL 4 " in line,
                      "HEL refresh after REHASH", start=start)
        services.send(f"DB {node.sid} HEL 4 ACK ? {services.epoch} OCL")
        services.barrier()
        assert host(observer, "alice") == "alice.test"
        assert host(voluntary, "voluntary") == changed_base

        services.mutation(2, "S::suffix")
        assert host(observer, "observer") == changed_base
        assert host(observer, "alice") == "alice.test"
        fallback = node.client("native-fallback")
        assert host(observer, "native-fallback") not in ("localhost", "127.0.0.1")
        assert "x" in modes(fallback, "native-fallback")
        assert "t" not in modes(fallback, "native-fallback")


def test_suspended_identity_cannot_restore_vhost_and_rehash_cannot_remove_guard(node_factory):
    node = node_factory(ready=False, prepare=seed_privacy_state,
                        name="privacy.test", sid="0T1", peers=["services.test"],
                        propagator="services.test", ulines=["services.test"],
                        vhost_flood_limit=100)
    with Peer(node, name="services.test", sid="0S1") as services:
        observer = node.client("observer")
        observer_base = host(observer, "observer")
        client = node.client("alice-setup")
        base = host(client, "alice-setup")
        identify(client)
        start = len(client.lines)
        services.mutation(1, "N::alice::suspend", "privacy policy")
        client.wait(lambda line: "This nickname is suspended. Reason: privacy policy" in line,
                    "nickname suspension", start=start)
        services.send("SVS2MODE alice +r")
        services.barrier()
        client.send("MODE alice +t")
        assert "t" not in modes(client, "alice")
        assert host(observer, "alice") == base

        config = node.config.read_text()
        assert 'loadmodule "third/udb";' in config
        node.config.write_text(config.replace('loadmodule "third/udb";', ""))
        operator = node.client("privacy-oper")
        assert any(" 381 " in line for line in operator.request("OPER testoper operpass"))
        assert operator.request("REHASH", terminator=lambda line: " 382 " in line)

        observer.send("MODE observer +t")
        assert "t" not in modes(observer, "observer")
        assert host(observer, "observer") == observer_base
