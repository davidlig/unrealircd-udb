"""Strict DB numeric parsing is observed through real negotiated peers."""

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


@pytest.mark.parametrize("fault", [
    "nonhex_checksum", "short_checksum", "negative_timestamp", "leading_plus",
    "int64_overflow", "ullmax", "huge_overflow",
])
def test_invalid_inventory_checksum_and_timestamp_fields_are_rejected(node_factory, fault):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    before = {letter: (node.data_dir / f"udb_{letter}.db").read_bytes() for letter in "NCISLK"}
    digest = "0123456789abcdef" * 4
    checksum = "Z" * 64 if fault == "nonhex_checksum" else "deadbeef" if fault == "short_checksum" else digest
    timestamp = {
        "negative_timestamp": "-100",
        "leading_plus": "+1787720000",
        "int64_overflow": "9223372036854775808",
        "ullmax": "18446744073709551615",
        "huge_overflow": "999999999999999999999999999999999999",
    }.get(fault, "1787720000")
    round_id = 100

    with Peer(node) as peer:
        start = len(peer.lines)
        peer.send(f"DB {node.sid} INF {round_id} N {checksum} 0 {timestamp}")
        peer.wait(lambda line: " ERR INF " in line,
                  f"reject malformed INF {fault}", start=start)
        peer.barrier()
        assert not any(f" RES {round_id} N" in line for line in peer.lines[start:])
        assert node.state()["STATE"] == "READY"

    node.close()
    assert {letter: (node.data_dir / f"udb_{letter}.db").read_bytes() for letter in "NCISLK"} == before


def operator(node):
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    return client


def query(client, path):
    return "\n".join(client.request(f"DBQ {path}"))


@pytest.mark.parametrize("value", [
    "*", "*123a", "*-5", "*999999999999999999999999999999999999",
])
def test_invalid_numeric_insert_is_rejected_without_mutation(node_factory, value):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = operator(node)
    path = "I::127.0.0.1::clones"
    snapshot = node.data_dir / "udb_I.db"

    with Peer(node) as peer:
        peer.inventory()
        original = snapshot.read_bytes()
        start = len(peer.lines)
        peer.mutation(1, path, value)
        assert any(" ERR INS " in line for line in peer.lines[start:])
        assert "Block not found" in query(client, path)
        assert snapshot.read_bytes() == original

    node.close()
    assert snapshot.read_bytes() == original


def test_valid_numeric_insert_is_active_and_persisted(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = operator(node)
    path = "I::127.0.0.1::clones"
    with Peer(node) as peer:
        peer.inventory()
        peer.mutation(1, path, "*5")
        assert "DBQ I::127.0.0.1 5" in query(client, path)
    node.close()
    assert b"127.0.0.1::clones *5\n" in (node.data_dir / "udb_I.db").read_bytes()
