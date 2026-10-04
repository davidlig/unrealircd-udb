"""Real canonical snapshot transactions: private staging, validated durable publication."""

import pytest

from udb_test_support.peer import Peer, EMPTY_DIGEST
from udb_test_support.runtime import tree_digest

pytestmark = pytest.mark.protocol


def follower(node_factory, *, ready=True):
    return node_factory(ready=ready, peers=["peer.test"], propagator="peer.test",
                        expected_state="READY" if ready else "BOOTSTRAPPING")


def test_staged_data_is_private_until_end_and_valid_digest(node_factory):
    node = follower(node_factory)
    path = node.data_dir / "udb_N.db"
    original = path.read_bytes()
    records = {"alice::vhost": "alice.test"}
    with Peer(node) as peer:
        round_id, digest = peer.offer(records)
        peer.send(f"DB {node.sid} BEGIN {round_id} N private {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} N private alice::vhost alice.test")
        peer.barrier()
        assert path.read_bytes() == original
        start = len(peer.lines)
        peer.send(f"DB {node.sid} END {round_id} N private {digest}")
        peer.wait(lambda line: f" ACK {round_id} N private " in line, "committed N", start=start)
        assert b"alice::vhost alice.test\n" in path.read_bytes()
        assert tree_digest(records) == digest


@pytest.mark.parametrize("fault", ["wrong_end_digest", "wrong_content", "wrong_txid", "wrong_round"])
def test_invalid_snapshot_never_replaces_the_durable_active_block(node_factory, fault):
    node = follower(node_factory)
    path = node.data_dir / "udb_N.db"
    original = path.read_bytes()
    records = {"alice::vhost": "alice.test"}
    with Peer(node) as peer:
        round_id, digest = peer.offer(records)
        peer.send(f"DB {node.sid} BEGIN {round_id} N transaction {digest}")
        value = "different.test" if fault == "wrong_content" else "alice.test"
        peer.send(f"DB {node.sid} PUT {round_id} N transaction alice::vhost {value}")
        end_digest = "f" * 64 if fault == "wrong_end_digest" else digest
        txid = "unsolicited" if fault == "wrong_txid" else "transaction"
        ending_round = round_id + 1 if fault == "wrong_round" else round_id
        peer.send(f"DB {node.sid} END {ending_round} N {txid} {end_digest}")
        peer.barrier()
        assert path.read_bytes() == original
        assert not any(f" ACK {ending_round} N {txid} " in line for line in peer.lines)
    node.close()
    assert path.read_bytes() == original


@pytest.mark.parametrize("command", [
    "BEGIN 1 N unsolicited " + "f" * 64,
    "PUT 1 N unsolicited alice::vhost evil.test",
    "END 1 N unsolicited " + "f" * 64,
])
def test_unsolicited_snapshot_frames_cannot_publish(node_factory, command):
    node = follower(node_factory)
    path = node.data_dir / "udb_N.db"
    original = path.read_bytes()
    with Peer(node) as peer:
        peer.send(f"DB {node.sid} {command}")
        peer.barrier()
        assert path.read_bytes() == original
        assert not any(" ACK 1 N unsolicited " in line for line in peer.lines)


def test_bootstrap_requires_inventory_convergence_of_all_six_blocks(node_factory):
    node = follower(node_factory, ready=False)
    with Peer(node) as peer:
        for letter in "NCISL":
            peer.send(f"DB {node.sid} INF 1 {letter} {EMPTY_DIGEST} 0 0")
        peer.barrier()
        assert node.state()["STATE"] == "BOOTSTRAPPING"
        peer.send(f"DB {node.sid} INF 1 K {EMPTY_DIGEST} 0 0")
        peer.barrier()
        assert node.wait_for_state("READY")["STATE"] == "READY"
        assert all((node.data_dir / f"udb_{letter}.db").is_file() for letter in "NCISLK")
