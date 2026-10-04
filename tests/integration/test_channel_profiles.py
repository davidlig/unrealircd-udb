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
