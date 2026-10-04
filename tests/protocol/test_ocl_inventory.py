"""Independent inventory checksums and complete-frame OCLG observations."""

import pytest

from udb_test_support.peer import Peer, inventory_digest

pytestmark = pytest.mark.protocol


def local_inventory(peer):
    entries = []
    for line in peer.lines:
        fields = line.split()
        if len(fields) == 10 and fields[3:5] == ["OCL", "ITEM"] and fields[5] == peer.node.sid:
            entries.append((fields[8], fields[9]))
    assert entries
    assert entries == sorted(entries)
    return entries


def global_snapshots(lines):
    snapshots, current = [], None
    for line in lines:
        fields = line.split()
        if len(fields) < 5 or fields[3] != "OCLG":
            continue
        if fields[4] == "BEGIN":
            assert len(fields) == 10
            current = {"epoch": fields[5], "generation": fields[6], "state": fields[7],
                       "count": int(fields[8]), "entries": []}
        elif current is not None and fields[4] == "ITEM":
            assert len(fields) == 9
            assert fields[5:7] == [current["epoch"], current["generation"]]
            current["entries"].append((fields[7], fields[8]))
        elif current is not None and fields[4] == "END":
            assert fields[5:7] == [current["epoch"], current["generation"]]
            assert len(current["entries"]) == current["count"]
            assert current["entries"] == sorted(current["entries"])
            snapshots.append(current)
            current = None
    return snapshots


def test_unpublished_peer_inventory_keeps_global_view_incomplete(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        assert any(s["state"] == "INCOMPLETE" for s in global_snapshots(peer.lines))


@pytest.mark.parametrize("matching", [True, False])
def test_complete_remote_inventory_produces_exact_global_intersection(node_factory, matching):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        entries = local if matching else [("netadmin", "f" * 64)]
        start = len(peer.lines)
        peer.ocl(entries)
        snapshots = global_snapshots(peer.lines[start:])
        ready = [s for s in snapshots if s["state"] == "READY"]
        assert ready
        assert ready[-1]["entries"] == (local if matching else [])


@pytest.mark.parametrize("fault", ["digest", "count", "duplicate", "wrong_epoch"])
def test_invalid_remote_inventory_never_commits(node_factory, fault):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        peer.ocl(local)
        assert "Committed operclass inventory for origin 0P1: generation 1" in node.log_text()
        entries = local
        options = {}
        if fault == "digest":
            options["digest"] = "f" * 64
        elif fault == "count":
            options["count"] = len(local) + 1
        elif fault == "duplicate":
            entries = [local[0], local[0]]
        else:
            options["epoch"] = "2222222222222222"
        peer.ocl(entries, generation=2, **options)
        assert "Committed operclass inventory for origin 0P1: generation 2" not in node.log_text()


def test_matching_inventory_has_exact_intersection_with_reversed_wire_order(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        start = len(peer.lines)
        # END has a valid checksum for precisely the received wire inventory.
        # Once accepted, identical membership/fingerprints must yield the full intersection.
        peer.ocl(list(reversed(local)))
        ready = [s for s in global_snapshots(peer.lines[start:]) if s["state"] == "READY"]
        assert ready
        assert ready[-1]["entries"] == local


def test_stale_generation_cannot_replace_committed_inventory(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        peer.ocl(local, generation=2)
        start = len(peer.lines)
        peer.ocl([("netadmin", "f" * 64)], generation=1)
        peer.send("UDB OPERCLASS netadmin")
        peer.wait(lambda line: "Operclass netadmin: GLOBAL" in line, "unchanged global inventory", start=start)
        assert "Committed operclass inventory for origin 0P1: generation 1" not in node.log_text()


@pytest.mark.parametrize("fault", ["same_generation_different_digest", "replayed_old_generation"])
def test_conflicting_replay_cannot_replace_exact_global_membership(node_factory, fault):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        peer.ocl(local, generation=2)
        peer.ocl(local, generation=2)
        start = len(peer.lines)
        peer.ocl([("netadmin", "f" * 64)],
                 generation=2 if fault == "same_generation_different_digest" else 1)
        peer.send("UDB OPERCLASS netadmin")
        peer.wait(lambda line: "Operclass netadmin: GLOBAL" in line,
                  "replay preserves global class", start=start)
        assert not any(s["state"] == "READY" and s["entries"] != local
                       for s in global_snapshots(peer.lines[start:]))
        assert "Committed operclass inventory for origin 0P1: generation 1" not in node.log_text()


def test_partial_update_withdraws_ready_until_exact_authenticated_completion(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        peer.ocl(local)
        start = len(peer.lines)
        digest = inventory_digest(local)
        peer.send(f"DB * OCL BEGIN {peer.sid} {peer.epoch} 2 {len(local)} {digest}")
        name, fingerprint = local[0]
        peer.send(f"DB * OCL ITEM {peer.sid} {peer.epoch} 2 {name} {fingerprint}")
        peer.barrier()
        snapshots = global_snapshots(peer.lines[start:])
        assert snapshots and snapshots[-1]["state"] == "INCOMPLETE"
        for name, fingerprint in local[1:]:
            peer.send(f"DB * OCL ITEM {peer.sid} {peer.epoch} 2 {name} {fingerprint}")
        start = len(peer.lines)
        peer.send(f"DB * OCL END {peer.sid} {peer.epoch} 2")
        peer.barrier()
        snapshots = global_snapshots(peer.lines[start:])
        assert snapshots[-1]["state"] == "READY"
        assert snapshots[-1]["entries"] == local


def test_rehash_replays_inventory_without_reconnecting_or_changing_fingerprints(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node, oclg=True) as peer:
        local = local_inventory(peer)
        peer.ocl(local)
        client = node.client("rehashoper")
        assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
        links = node.log_text().count("Server linked:")
        start = len(peer.lines)
        replies = client.request("REHASH", terminator=lambda line: " 382 " in line or "Rehashing" in line)
        assert replies
        peer.wait(lambda line: " HEL 4 " in line, "REHASH capability refresh", start=start)
        peer.send(f"DB {node.sid} HEL 4 ACK ? {peer.epoch} OCL OCLG")
        peer.wait(lambda line: " OCL END " in line and f" {node.sid} " in line,
                  "automatic inventory replay after REHASH", start=start, timeout=15)
        # The same live connection confirms HEL and resubmits its authenticated
        # inventory: no reconnect or retained stale READY view is accepted.
        peer.ocl(local)
        replay = []
        for line in peer.lines[start:]:
            fields = line.split()
            if len(fields) == 10 and fields[3:5] == ["OCL", "ITEM"] and fields[5] == node.sid:
                replay.append((fields[8], fields[9]))
        assert replay == local
        ready = [s for s in global_snapshots(peer.lines[start:]) if s["state"] == "READY"]
        assert ready and ready[-1]["entries"] == local
        assert node.log_text().count("Server linked:") == links
