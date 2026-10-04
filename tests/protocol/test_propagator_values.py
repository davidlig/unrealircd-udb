"""Validate sequenced real-peer writes to the persisted propagator list."""

import pytest

from udb_test_support.peer import Peer


pytestmark = pytest.mark.protocol


def test_propagator_list_rejects_bad_entries_and_accepts_long_canonical_lists(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    snapshot = node.data_dir / "udb_S.db"
    with Peer(node) as peer:
        peer.inventory()
        baseline = snapshot.read_bytes()
        invalid = (
            "",
            ",a.net",
            "a.net,",
            "a.net,,b.net",
            "a.net,   ,b.net",
            ("a" * 60) + ".net",
            "a.net," + ("b" * 60) + ".net",
            "host\rwith\rCR.net",
            "host\nwith\nLF.net",
            "host\twith\ttab.net",
            "invalid!host.net",
            "bad@domain.com,valid.net",
            "srv:6667,hub.net",
            "nodot",
        )
        sequence = 0
        for value in invalid:
            if "\r" in value or "\n" in value:
                with pytest.raises(ValueError, match="IRC command must contain one line"):
                    peer.mutation(sequence + 1, "S::propagator", value)
                assert snapshot.read_bytes() == baseline
                continue
            sequence += 1
            start = len(peer.lines)
            peer.mutation(sequence, "S::propagator", value)
            assert any(" DB " in line and " ERR INS " in line for line in peer.lines[start:]), value
            assert snapshot.read_bytes() == baseline, f"invalid list changed the durable setting: {value!r}"

        tokens_512 = [f"srv-{index:03d}.example.net" for index in range(35)]
        list_512 = ",".join(tokens_512)
        assert len(list_512) > 550

        tokens_4k = []
        while True:
            candidate = f"long-node-{len(tokens_4k):04d}.example.net"
            proposed = ",".join((*tokens_4k, candidate))
            if len(proposed) > 4096:
                break
            tokens_4k.append(candidate)
        list_4k = ",".join(tokens_4k)
        assert 4000 <= len(list_4k) <= 4096

        valid = (
            "a.example.net",
            "a.example.net,b.example.net",
            "  a.example.net  ,  b.example.net  ",
            list_512,
            list_4k,
        )
        for value in valid:
            sequence += 1
            start = len(peer.lines)
            peer.mutation(sequence, "S::propagator", value)
            assert not any(" ERR INS " in line for line in peer.lines[start:]), value[:80]
            expected = f"propagator {value}\n".encode("ascii")
            assert expected in snapshot.read_bytes(), f"valid S::propagator was not persisted exactly ({len(value)})"
