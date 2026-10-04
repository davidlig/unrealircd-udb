"""Real wire-level coverage for canonical IPv6 paths and path rejection."""

import pytest

from udb_test_support.peer import Peer


pytestmark = pytest.mark.protocol


IPV6_RECORDS = {
    "I": {
        "2001%3Adb8%3A%3A1::clones *10",
        "2001%3Adb8%3A%3A1::nolines GZ",
        "2001%3Adb8%3A%3A1::host ipv6.example.test",
    },
    "K": {
        "Z::2001%3Adb8%3A%3A2::reason IPv6 Z-line test",
        "G::*@2001%3Adb8%3A%3A3::reason IPv6 G-line test",
    },
}


def persisted_blocks(node):
    return {
        letter: (node.data_dir / f"udb_{letter}.db").read_bytes()
        for letter in "NCISLK"
    }


def test_encoded_ipv6_paths_round_trip_and_reload(node_factory):
    node = node_factory(
        ready=False,
        peers=["peer.test"],
        propagator="peer.test",
        expected_state="BOOTSTRAPPING",
    )
    runtime_path = node.path

    with Peer(node) as peer:
        peer.inventory()
        node.wait_for_state("READY")

        mutations = (
            ("I::2001%3Adb8%3A%3A1::clones", "*10"),
            ("I::2001%3Adb8%3A%3A1::nolines", "GZ"),
            ("I::2001%3Adb8%3A%3A1::host", "ipv6.example.test"),
            ("K::Z::2001%3Adb8%3A%3A2::reason", "IPv6 Z-line test"),
            ("K::G::*@2001%3Adb8%3A%3A3::reason", "IPv6 G-line test"),
        )
        for sequence, (path, value) in enumerate(mutations, start=1):
            peer.mutation(sequence, path, value)

    before_restart = persisted_blocks(node)
    for letter, records in IPV6_RECORDS.items():
        text = before_restart[letter].decode("ascii")
        for record in records:
            assert record in text, f"canonical IPv6 record missing from block {letter}: {record}"

    node.close()
    restarted = node_factory(ready=False, path=runtime_path)
    assert restarted.wait_for_state("READY")["STATE"] == "READY"
    assert persisted_blocks(restarted) == before_restart
    for letter, records in IPV6_RECORDS.items():
        text = (restarted.data_dir / f"udb_{letter}.db").read_text(encoding="ascii")
        for record in records:
            assert record in text, f"reloaded block {letter} lost canonical IPv6 record: {record}"


def test_malformed_noncanonical_and_overlong_paths_fail_closed(node_factory):
    node = node_factory(
        ready=False,
        peers=["peer.test"],
        propagator="peer.test",
        expected_state="BOOTSTRAPPING",
    )
    with Peer(node) as peer:
        peer.inventory()
        node.wait_for_state("READY")
        baseline = persisted_blocks(node)
        invalid = (
            ("I::2001%3::clones", "*5", "I"),
            ("I::test%00bad::clones", "*5", "I"),
            ("I::::bad::clones", "*5", "I"),
            ("I::trailing::", "*5", "I"),
            ("K::G::*@2001:db8:0:0:0:0:0:4::reason", "raw IPv6 must reject", "K"),
            (f"I::{'x' * 4096}::clones", "*5", "I"),
        )
        for sequence, (path, value, letter) in enumerate(invalid, start=1):
            start = len(peer.lines)
            peer.mutation(sequence, path, value)
            assert any(
                " DB " in line and " ERR INS " in line and f" {letter}" in line
                for line in peer.lines[start:]
            ), f"malformed path was not rejected as ERR INS for block {letter}: {path[:80]}"
            assert persisted_blocks(node) == baseline, f"rejected path changed durable state: {path[:80]}"
