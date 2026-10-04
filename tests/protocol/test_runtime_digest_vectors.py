"""Verify canonical empty/single/multi-record hashes through runtime DBQ."""

import re

import pytest

from udb_test_support.peer import Peer


pytestmark = pytest.mark.protocol

EMPTY = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
SINGLE = "5819874dff9e29994db2eeb8b949bb63cf5c213303539d35b8a6bbaa05088049"
MULTI = "e9f2a16960fa2fcaed0f9d72cfeabead6ccf0415ad02ce1ae9303b96fffe22ff"


def block_digest(client):
    replies = client.request("DBQ N")
    rows = [line for line in replies if " 339 " in line]
    assert len(rows) == 1, f"DBQ N returned no unique manifest row: {replies}"
    digests = re.findall(r"(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])", rows[0])
    assert len(digests) == 1, f"DBQ N returned no unique lowercase SHA-256: {rows[0]}"
    return digests[0]


def test_dbq_reports_empty_single_and_order_independent_golden_digests(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))

    with Peer(node) as peer:
        peer.inventory()
        assert block_digest(client) == EMPTY

        peer.mutation(1, "N::user1::vhost", "user1.org")
        assert block_digest(client) == SINGLE

        # Deliberately insert user3 before user2: the manifest must hash the
        # canonical sorted tree, not the live insertion order.
        peer.mutation(2, "N::user3::vhost", "user3.org")
        peer.mutation(3, "N::user2::vhost", "user2.org")
        assert block_digest(client) == MULTI
