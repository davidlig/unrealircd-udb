"""Sequenced mutations against a real follower, with live queries and durable proof."""

import pytest

from udb_test_support.peer import Peer
from udb_test_support.runtime import tree_digest

pytestmark = pytest.mark.protocol


def operator(node):
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    return client


def query(client, path):
    return "\n".join(client.request(f"DBQ {path}"))


def test_ordered_insert_delete_and_replay_do_not_replace_live_values(node_factory):
    node = node_factory(ready=False, peers=["peer.test"], propagator="peer.test",
                        expected_state="BOOTSTRAPPING")
    with Peer(node) as peer:
        peer.inventory()
        node.wait_for_state("READY")
        client = operator(node)
        status = "\n".join(client.request("UDB STATUS"))
        assert "Database readiness: READY" in status
        assert "synchronization: OK" in status
        peer.mutation(1, "N::alice::vhost", "alice.test")
        assert "alice.test" in query(client, "N::alice::vhost")
        assert "alice.test" in query(client, "N::alice::vhost")
        peer.mutation(1, "N::alice::vhost", "replay.test")
        reply = query(client, "N::alice::vhost")
        assert "alice.test" in reply and "replay.test" not in reply
        peer.mutation(2, "N::alice::vhost")
        assert "Block not found" in query(client, "N::alice::vhost")
        peer.mutation(2, "N::alice::vhost", "resurrect.test")
        assert "Block not found" in query(client, "N::alice::vhost")


def test_gap_isolated_then_six_block_watermark_recovery_resumes_sequence(node_factory):
    node = node_factory(ready=False, peers=["peer.test"], propagator="peer.test",
                        expected_state="BOOTSTRAPPING")
    with Peer(node) as peer:
        peer.inventory()
        node.wait_for_state("READY")
        client = operator(node)
        peer.mutation(1, "N::alice::vhost", "alice.test")
        start = len(peer.lines)
        peer.mutation(3, "N::carol::vhost", "carol.test")
        assert "Block not found" in query(client, "N::carol::vhost")
        assert "synchronization: DEGRADED" in "\n".join(client.request("UDB STATUS"))
        peer.wait(lambda line: " HEL 4 " in line, "gap repair HEL", start=start)
        records = {f"{nick}::vhost": f"{nick}.test" for nick in ("alice", "bob", "carol")}
        round_id = peer.inventory({"N": records}, watermark=3)
        checksum = tree_digest(records)
        peer.wait(lambda line: f" RES {round_id} N" in line, "gap repair snapshot")
        peer.send(f"DB {node.sid} BEGIN {round_id} N repair {checksum} 3")
        for path, value in records.items():
            peer.send(f"DB {node.sid} PUT {round_id} N repair {path} :{value}")
        start = len(peer.lines)
        peer.send(f"DB {node.sid} END {round_id} N repair {checksum} 3")
        peer.wait(lambda line: f" ACK {round_id} N repair " in line, "repair commit", start=start)
        peer.barrier()
        status = "\n".join(client.request("UDB STATUS"))
        assert "Database readiness: READY" in status
        assert "synchronization: OK" in status
        for nick in ("alice", "bob", "carol"):
            assert f"{nick}.test" in query(client, f"N::{nick}::vhost")
        peer.mutation(4, "N::dave::vhost", "dave.test")
        assert "dave.test" in query(client, "N::dave::vhost")
    node.close()
    durable = (node.data_dir / "udb_N.db").read_text()
    assert all(f"{nick}::vhost {nick}.test\n" in durable for nick in ("alice", "bob", "carol", "dave"))


@pytest.mark.parametrize("fault", ["unnegotiated", "wrong_epoch", "unauthorized"])
def test_ineligible_mutation_origin_cannot_modify_active_or_durable_data(node_factory, fault):
    node = node_factory(ready=True, peers=["peer.test"],
                        propagator="other.test" if fault == "unauthorized" else "peer.test")
    before = (node.data_dir / "udb_N.db").read_bytes()
    client = operator(node)
    with Peer(node, negotiate=fault != "unnegotiated") as peer:
        # HEL carries the direct peer's OCL epoch, not necessarily the original
        # mutation source's epoch. Establish the accepted stream before changing it.
        if fault == "wrong_epoch":
            peer.inventory()
            peer.mutation(1, "N::trusted::vhost", "trusted.test")
            assert "trusted.test" in query(client, "N::trusted::vhost")
        peer.mutation(2 if fault == "wrong_epoch" else 1, "N::alice::vhost", "evil.test",
                      epoch="2222222222222222" if fault == "wrong_epoch" else None)
        assert "Block not found" in query(client, "N::alice::vhost")
        if fault != "wrong_epoch":
            assert (node.data_dir / "udb_N.db").read_bytes() == before
    node.close()
    durable = (node.data_dir / "udb_N.db").read_bytes()
    assert b"evil.test" not in durable
    if fault == "wrong_epoch":
        assert b"trusted::vhost trusted.test\n" in durable
    else:
        assert durable == before
