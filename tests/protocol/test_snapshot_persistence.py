"""Real filesystem faults at the snapshot's pre-commit boundary."""

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


@pytest.mark.parametrize("obstruction", ["file", "directory", "symlink"])
def test_existing_temporary_path_cannot_publish_or_overwrite_other_files(node_factory, obstruction):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    original = {letter: (node.data_dir / f"udb_{letter}.db").read_bytes()
                for letter in "NCISLK"}
    sentinel = node.data_dir / "protected.txt"
    sentinel.write_bytes(b"must not be overwritten\n")
    temporary = node.data_dir / "udb_N.db.tmp"
    with Peer(node) as peer:
        round_id, digest = peer.offer({"alice::vhost": "alice.test"})
        peer.send(f"DB {node.sid} BEGIN {round_id} N blocked {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} N blocked alice::vhost :alice.test")
        peer.barrier()
        if obstruction == "file":
            temporary.write_bytes(b"existing temporary file\n")
        elif obstruction == "directory":
            temporary.mkdir()
        else:
            temporary.symlink_to(sentinel.name)
        start = len(peer.lines)
        peer.send(f"DB {node.sid} END {round_id} N blocked {digest}")
        peer.barrier()
        assert not any(f" ACK {round_id} N blocked " in line for line in peer.lines[start:])
        assert any("Block not found" in line for line in client.request("DBQ N::alice::vhost"))
        assert all((node.data_dir / f"udb_{letter}.db").read_bytes() == before
                   for letter, before in original.items())
        assert sentinel.read_bytes() == b"must not be overwritten\n"
        if obstruction == "directory":
            temporary.rmdir()
        else:
            temporary.unlink()
        assert " ACK " in peer.transfer({"alice::vhost": "recovered.test"}, txid="retry")
        assert any("recovered.test" in line for line in client.request("DBQ N::alice::vhost"))
    node.close()
    assert b"alice::vhost recovered.test\n" in (node.data_dir / "udb_N.db").read_bytes()
    assert sentinel.read_bytes() == b"must not be overwritten\n"


def test_committed_snapshot_is_recovered_after_clean_daemon_restart(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))

    with Peer(node) as peer:
        reply = peer.transfer({"alice::vhost": "durable.test"}, txid="restart")
        assert " ACK " in reply
    assert any("durable.test" in line for line in client.request("DBQ N::alice::vhost"))

    runtime_path = node.path
    persisted = (node.data_dir / "udb_N.db").read_bytes()
    assert b"alice::vhost durable.test\n" in persisted
    node.close()

    restarted = node_factory(ready=False, path=runtime_path)
    restarted.wait_for_state("READY")
    recovered_client = restarted.client("recovered")
    assert any(" 381 " in line for line in recovered_client.request("OPER testoper operpass"))
    assert any("durable.test" in line
               for line in recovered_client.request("DBQ N::alice::vhost"))
    assert (restarted.data_dir / "udb_N.db").read_bytes() == persisted
