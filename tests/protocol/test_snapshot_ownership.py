"""Transaction identifiers retain exact bounds and identity on the real wire."""

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


def test_selected_authority_begin_requires_matching_request(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    original = (node.data_dir / "udb_C.db").read_bytes()
    with Peer(node) as peer:
        start = len(peer.lines)
        peer.send(f"DB {node.sid} BEGIN 1 C unrequested {'f' * 64}")
        peer.wait(lambda line: " ERR BEGIN " in line,
                  "selected authority still needs matching RES", start=start)
        peer.barrier()
        assert not any(" ACK 1 C unrequested " in line for line in peer.lines[start:])
        assert (node.data_dir / "udb_C.db").read_bytes() == original
    node.close()
    assert (node.data_dir / "udb_C.db").read_bytes() == original


@pytest.mark.parametrize("fault", ["schema", "digest"])
def test_owner_invalid_candidate_is_explicitly_rejected_without_publication(node_factory, fault):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    original = (node.data_dir / "udb_N.db").read_bytes()
    with Peer(node) as peer:
        round_id, digest = peer.offer({"alice::vhost": "alice.test"})
        peer.send(f"DB {node.sid} BEGIN {round_id} N invalid {digest}")
        start = len(peer.lines)
        path = "alice::challenge" if fault == "schema" else "alice::vhost"
        peer.send(f"DB {node.sid} PUT {round_id} N invalid {path} :alice.test")
        if fault == "schema":
            peer.wait(lambda line: " ERR PUT " in line, "invalid schema rejected", start=start)
        else:
            peer.send(f"DB {node.sid} END {round_id} N invalid {'f' * 64}")
            peer.wait(lambda line: " ERR END " in line or f" ABORT {round_id} N invalid " in line,
                      "invalid digest rejected", start=start)
        peer.barrier()
        assert (node.data_dir / "udb_N.db").read_bytes() == original
        assert not any(f" ACK {round_id} N invalid " in line for line in peer.lines[start:])
    node.close()
    assert (node.data_dir / "udb_N.db").read_bytes() == original


@pytest.mark.parametrize("command", ["BEGIN", "PUT", "END"])
def test_foreign_peer_cannot_hijack_or_abort_owner_transaction(node_factory, command):
    node = node_factory(ready=True, peers=["peer.test", "foreign.test"], propagator="peer.test")
    original = (node.data_dir / "udb_C.db").read_bytes()
    with Peer(node) as owner, Peer(node, name="foreign.test", sid="0F1") as foreign:
        round_id, digest = owner.offer({"#legit::topic": "owned topic"}, letter="C")
        owner.send(f"DB {node.sid} BEGIN {round_id} C owned {digest}")
        owner.barrier()
        start = len(foreign.lines)
        payload = {
            "BEGIN": f"BEGIN {round_id} C competing {digest}",
            "PUT": f"PUT {round_id} C owned #hijack::topic :injected topic",
            "END": f"END {round_id} C owned {digest}",
        }[command]
        foreign.send(f"DB {node.sid} {payload}")
        codes = (4, 6) if command == "BEGIN" else (5, 6)
        foreign.wait(lambda line: any(f" ERR {command} {code} " in line for code in codes),
                     "foreign transaction frame rejected", start=start)
        foreign.barrier()
        assert (node.data_dir / "udb_C.db").read_bytes() == original
        owner.send(f"DB {node.sid} PUT {round_id} C owned #legit::topic :owned topic")
        start = len(owner.lines)
        owner.send(f"DB {node.sid} END {round_id} C owned {digest}")
        owner.wait(lambda line: f" ACK {round_id} C owned " in line,
                   "owner survives foreign frame", start=start)
        assert not any(f" ACK {round_id} C " in line for line in foreign.lines)
    node.close()
    persisted = (node.data_dir / "udb_C.db").read_bytes()
    assert b"#legit::topic owned topic\n" in persisted
    assert b"#hijack" not in persisted


def test_maximum_transaction_identifier_commits_without_truncation(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer({"alice::vhost": "alice.test"}, txid="a" * 31)
    node.close()
    assert b"alice::vhost alice.test\n" in (node.data_dir / "udb_N.db").read_bytes()


def test_overlong_identifier_rejected_then_same_offer_accepts_valid_begin(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    original = (node.data_dir / "udb_N.db").read_bytes()
    with Peer(node) as peer:
        round_id, digest = peer.offer({"alice::vhost": "alice.test"})
        start = len(peer.lines)
        peer.send(f"DB {node.sid} BEGIN {round_id} N {'a' * 32} {digest}")
        peer.wait(lambda line: " ERR " in line and " BEGIN 2" in line,
                  "overlong transaction rejected", start=start)
        assert (node.data_dir / "udb_N.db").read_bytes() == original
        txid = "a" * 31
        peer.send(f"DB {node.sid} BEGIN {round_id} N {txid} {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} N {txid} alice::vhost :alice.test")
        start = len(peer.lines)
        peer.send(f"DB {node.sid} END {round_id} N {txid} {digest}")
        peer.wait(lambda line: f" ACK {round_id} N {txid} " in line,
                  "valid identifier after rejected BEGIN", start=start)
    node.close()
    assert b"alice::vhost alice.test\n" in (node.data_dir / "udb_N.db").read_bytes()


def test_maximum_identifiers_differing_at_last_byte_are_not_aliases(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    original = (node.data_dir / "udb_N.db").read_bytes()
    with Peer(node) as peer:
        round_id, digest = peer.offer({"alice::vhost": "alice.test"})
        owner, foreign = "a" * 30 + "x", "a" * 30 + "y"
        peer.send(f"DB {node.sid} BEGIN {round_id} N {owner} {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} N {foreign} alice::vhost :evil.test")
        peer.send(f"DB {node.sid} END {round_id} N {foreign} {digest}")
        peer.barrier()
        assert any(f" ERR PUT 5 {round_id} N" in line for line in peer.lines)
        assert (node.data_dir / "udb_N.db").read_bytes() == original
        assert not any(f" ACK {round_id} N {foreign} " in line for line in peer.lines)
        assert " ACK " in peer.transfer({"alice::vhost": "recovered.test"}, txid=owner)
    node.close()
    assert b"alice::vhost recovered.test\n" in (node.data_dir / "udb_N.db").read_bytes()
