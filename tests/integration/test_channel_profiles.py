"""Committed channel profiles affect real JOIN, NAMES, MODE and TOPIC replies."""

import hashlib
import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.integration


def request(client, command, code):
    return client.request(command, terminator=lambda line: f" {code} " in line)


def test_authenticated_founder_receives_owner_rank_and_committed_topic(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        assert " ACK " in peer.transfer({"#vault::founder": "alice", "#vault::topic": "committed topic"}, letter="C")
        client = node.client("visitor")
        replies = client.request("NICK alice:secret")
        assert any("You are now identified" in line for line in replies)
        replies = request(client, "JOIN #vault", 366)
        # Native JOIN NAMES precedes UDB's post-JOIN rank reconciliation.
        replies = request(client, "NAMES #vault", 366)
        assert any(" 353 " in line and "~alice" in line for line in replies), replies
        assert not any(" 353 " in line and "@alice" in line for line in replies), replies
        replies = request(client, "TOPIC #vault", 332)
        assert any(line.endswith(":committed topic") for line in replies), replies
        replies = request(client, "MODE #vault", 324)
        assert "r" in next(line.split()[4] for line in replies if " 324 " in line)


def test_staged_topic_does_not_change_existing_channel_until_valid_commit(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        initial = {"#room::topic": "original topic"}
        assert " ACK " in peer.transfer(initial, letter="C")
        client = node.client("visitor")
        request(client, "JOIN #room", 366)
        records = {"#room::topic": "candidate topic"}
        round_id, digest = peer.offer(records, letter="C")
        peer.send(f"DB {node.sid} BEGIN {round_id} C private {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} C private #room::topic :candidate topic")
        peer.barrier()
        assert any(line.endswith(":original topic") for line in request(client, "TOPIC #room", 332))
        peer.send(f"DB {node.sid} END {round_id} C private {digest}")
        peer.barrier()
        assert any(line.endswith(":candidate topic") for line in request(client, "TOPIC #room", 332))
    node.close()
    content = (node.data_dir / "udb_C.db").read_text()
    assert "#room::topic candidate topic\n" in content
    assert "original topic" not in content


@pytest.mark.parametrize("mode,key,code", [("+k secret", "", 475), ("+k secret", "wrong", 475), ("+i", "", 473)])
def test_first_join_materializes_stored_restrictions_before_admission(node_factory, mode, key, code):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"#restricted::modes": mode}, letter="C")
        client = node.client("visitor")
        replies = request(client, f"JOIN #restricted {key}".rstrip(), code)
        assert any(f" {code} " in line for line in replies)
        assert not any(" JOIN :#restricted" in line or " 366 " in line for line in replies)
        if mode.startswith("+k"):
            replies = request(client, "JOIN #restricted secret", 366)
            assert any(" JOIN :#restricted" in line for line in replies)


def test_unregistered_user_never_receives_configured_founder_rank(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"#room::founder": "alice"}, letter="C")
        client = node.client("alice")
        request(client, "JOIN #room", 366)
        replies = request(client, "NAMES #room", 366)
        assert any(" 353 " in line and "alice" in line for line in replies)
        assert not any(" 353 " in line and any(prefix + "alice" in line for prefix in ("~", "&", "@")) for line in replies)


def channel_members(client):
    replies = request(client, "NAMES #room", 366)
    return {member for line in replies if " 353 " in line for member in line.split(" :", 1)[1].split()}


@pytest.mark.parametrize("options", [0, 2, 32, 34])
def test_identified_founder_recovers_own_rank_after_join_without_rejoin(node_factory, options):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        assert " ACK " in peer.transfer({"#room::founder": "alice", "#room::options": f"*{options}"}, letter="C")
        client = node.client("visitor")
        request(client, "JOIN #room", 366)
        assert channel_members(client) == {"visitor"}
        start = len(client.lines)
        replies = client.request("NICK alice:secret")
        assert any("You are now identified" in line for line in replies), replies
        assert channel_members(client) == {"alice"}
        replies = client.request("MODE #room +q ALICE")
        assert any(line.endswith(" MODE #room +q alice") for line in replies), replies
        assert channel_members(client) == {"~alice"}
        replies = client.request("MODE #room +q alice")
        assert not any(" MODE #room " in line for line in replies), replies
        assert channel_members(client) == {"~alice"}
        assert not any(" JOIN " in line or " PART " in line for line in client.lines[start:])


@pytest.mark.parametrize("options", [0, 32])
@pytest.mark.parametrize("command", [
    "MODE #room +q visitor2",
    "MODE #room +qo alice visitor2",
    "MODE #room -q+q alice alice",
    "MODE #room +q",
    "MODE #room +q alice visitor2",
    "MODE #room +q :alice visitor2",
    "MODE #room +q nobody",
    "SAMODE #room +q alice",
])
def test_founder_recovery_does_not_elevate_other_mode_commands(node_factory, options, command):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        assert " ACK " in peer.transfer({"#room::founder": "alice", "#room::options": f"*{options}"}, letter="C")
        client = node.client("visitor")
        request(client, "JOIN #room", 366)
        other = node.client("visitor2")
        request(other, "JOIN #room", 366)
        assert any("You are now identified" in line for line in client.request("NICK alice:secret"))
        assert channel_members(client) == {"alice", "visitor2"}
        client.request(command)
        assert channel_members(client) == {"alice", "visitor2"}


@pytest.mark.parametrize("identified", [False, True])
def test_self_recovery_requires_identified_founder(node_factory, identified):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        if identified:
            assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        founder = "someoneelse" if identified else "alice"
        assert " ACK " in peer.transfer({"#room::founder": founder, "#room::options": "*32"}, letter="C")
        client = node.client("visitor" if identified else "alice")
        request(client, "JOIN #room", 366)
        if identified:
            assert any("You are now identified" in line for line in client.request("NICK alice:secret"))
        client.request("MODE #room +q alice")
        assert channel_members(client) == {"alice"}


def test_founder_self_recovery_requires_channel_membership(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        assert " ACK " in peer.transfer({"#room::founder": "alice"}, letter="C")
        other = node.client("visitor2")
        request(other, "JOIN #room", 366)
        client = node.client("visitor")
        assert any("You are now identified" in line for line in client.request("NICK alice:secret"))
        replies = client.request("MODE #room +q alice")
        assert not any(" MODE #room +q " in line for line in replies), replies
        assert channel_members(other) == {"visitor2"}


def test_founder_self_recovery_respects_channel_suspension(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest()})
        assert " ACK " in peer.transfer({"#room::founder": "alice", "#room::suspend": "review"}, letter="C")
        other = node.client("visitor2")
        request(other, "JOIN #room", 366)
        client = node.client("visitor")
        request(client, "JOIN #room", 366)
        assert any("You are now identified" in line for line in client.request("NICK alice:secret"))
        client.request("MODE #room +q alice")
        assert "alice" in channel_members(client)
        peer.mutation(1, "C::#room::suspend")
        assert "~alice" in channel_members(client)


def test_founder_recovers_after_nick_unsuspension_and_reauthentication_without_rejoin(node_factory):
    node = node_factory(peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest(),
                                        "alice::suspend": "review"})
        assert " ACK " in peer.transfer({"#room::founder": "alice", "#room::options": "*32"}, letter="C")
        client = node.client("visitor")
        request(client, "JOIN #room", 366)
        start = len(client.lines)
        replies = client.request("NICK alice:secret")
        assert not any("You are now identified" in line for line in replies), replies
        client.request("MODE #room +q alice")
        assert channel_members(client) == {"alice"}
        peer.mutation(1, "N::alice::suspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line, "unsuspend rename", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        client.request(f"MODE #room +q {guest}")
        assert channel_members(client) == {guest}
        assert any("You are now identified" in line for line in client.request("NICK alice:secret"))
        assert channel_members(client) == {"alice"}
        client.request("MODE #room +q alice")
        assert channel_members(client) == {"~alice"}
        assert not any(" JOIN " in line or " PART " in line for line in client.lines[start:])
