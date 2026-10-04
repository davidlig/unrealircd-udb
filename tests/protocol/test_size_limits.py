"""Live value, key-component and BIGLINES bounds against real UDB peers."""

import base64

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


def operator(node):
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    return client


def query(client, path):
    return "\n".join(client.request(f"DBQ {path}"))


def encoded_pattern(length, fill):
    suffix = f"p{length}end"
    raw = (fill * (length - len(suffix))) + suffix
    assert len(raw) == length
    return raw, "b64%3A" + base64.b64encode(raw.encode("ascii")).decode("ascii")


def test_value_path_and_line_boundaries_survive_durable_restart(node_factory):
    values = {
        f"#chan{size}::topic": (letter * (size - 1)) + str(size % 10)
        for size, letter in ((500, "A"), (4095, "B"), (4096, "C"))
    }
    raw_pattern, component = encoded_pattern(3072, "w")
    large_rule = "K::F::" + component + "::reason"
    large_value = "SpamReason_" + ("D" * (4096 - len("SpamReason_")))
    runtime_path = None
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = operator(node)
    with Peer(node) as peer:
        peer.inventory()
        for sequence, (path, value) in enumerate(values.items(), 1):
            peer.mutation(sequence, "C::" + path, value)
            assert value[:100] in query(client, "C::" + path)
        start = len(peer.lines)
        peer.mutation(len(values) + 1, large_rule, large_value)
        assert not any(" ERR INS " in line for line in peer.lines[start:])
    runtime_path = node.path
    node.close()

    c_path, k_path = node.data_dir / "udb_C.db", node.data_dir / "udb_K.db"
    c_before, k_before = c_path.read_bytes(), k_path.read_bytes()
    assert all(f"{path} {value}\n".encode("ascii") in c_before for path, value in values.items())
    assert f"{large_rule.removeprefix('K::')} {large_value}\n".encode("ascii") in k_before

    restarted = node_factory(ready=False, path=runtime_path, expected_state="READY")
    client = operator(restarted)
    for path, value in values.items():
        assert value[:100] in query(client, "C::" + path)
    restarted.close()
    assert c_path.read_bytes() == c_before
    assert k_path.read_bytes() == k_before
    assert len(raw_pattern) == 3072


@pytest.mark.parametrize("size", [4097, 5000, 7000])
def test_oversized_values_are_rejected_without_active_or_durable_mutation(node_factory, size):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = operator(node)
    snapshot = node.data_dir / "udb_C.db"
    with Peer(node) as peer:
        peer.inventory()
        original = snapshot.read_bytes()
        path = f"C::#bad{size}::topic"
        start = len(peer.lines)
        peer.mutation(1, path, "X" * size)
        assert any(" ERR INS " in line for line in peer.lines[start:])
        assert "Block not found" in query(client, path)
        assert snapshot.read_bytes() == original
    node.close()
    assert snapshot.read_bytes() == original


def test_spamfilter_decoded_length_boundary_and_oversize_rejection(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    snapshot = node.data_dir / "udb_K.db"
    with Peer(node) as peer:
        peer.inventory()
        for sequence, length in enumerate((3071, 3072), 1):
            _, component = encoded_pattern(length, "a" if length == 3071 else "b")
            path = f"K::F::{component}::reason"
            value = f"Reason{length}"
            start = len(peer.lines)
            peer.mutation(sequence, path, value)
            assert not any(" ERR INS " in line for line in peer.lines[start:])

        _, oversized_component = encoded_pattern(3073, "c")
        invalid = [
            (f"K::F::{oversized_component}::reason", "Reason3073"),
            ("K::F::" + ("plain_" * 600) + "::reason", "ReasonPlain"),
        ]
        for sequence, (path, value) in enumerate(invalid, 3):
            start = len(peer.lines)
            peer.mutation(sequence, path, value)
            assert any(" ERR INS " in line for line in peer.lines[start:])

    node.close()
    contents = snapshot.read_text(encoding="ascii")
    assert "Reason3071" in contents and "Reason3072" in contents
    assert "Reason3073" not in contents and "ReasonPlain" not in contents
