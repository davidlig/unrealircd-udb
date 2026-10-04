"""Snapshots are hop-by-hop; the original authorized live stream crosses A-B-C."""

import base64
import hashlib
import time

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


def oper(node, nickname):
    client = node.client(nickname)
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    return client


def wait_for_records(nodes, records, timeout=15):
    deadline = time.monotonic() + timeout
    last = {}
    while time.monotonic() < deadline:
        complete = True
        for node in nodes:
            for letter, expected in records.items():
                path = node.data_dir / f"udb_{letter}.db"
                try:
                    text = path.read_text(encoding="ascii")
                except OSError:
                    complete = False
                    continue
                last[(str(node.path), letter)] = text
                complete &= all(f"{record}\n" in text for record in expected)
        if complete:
            return
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for multi-hop durable records: {last}")


def userhost(client, nickname):
    replies = client.request(f"WHOIS {nickname}", terminator=lambda line: " 318 " in line)
    return next(line.split()[5] for line in replies if " 311 " in line)


def wait_for_userhost(client, nickname, expected, timeout=10):
    deadline = time.monotonic() + timeout
    observed = []
    while time.monotonic() < deadline:
        current = userhost(client, nickname)
        observed.append(current)
        if current == expected:
            return
    raise AssertionError(f"{nickname} host did not converge to {expected!r}: {observed}")


def usermodes(client, nickname):
    replies = client.request(f"MODE {nickname}", terminator=lambda line: " 221 " in line)
    return set(next(line.rsplit(" ", 1)[-1].lstrip(":+")
                    for line in replies if " 221 " in line))


def test_three_nodes_import_hop_by_hop_and_relay_original_stream_with_live_effects(node_factory):
    c = node_factory(ready=False, name="c.test", sid="0T3", peers=["b.test"],
                     propagator="b.test", expected_state="BOOTSTRAPPING")
    b = node_factory(ready=False, name="b.test", sid="0T2", peers=["a.test", "c.test"],
                     outgoing={"c.test": c.server_port}, propagator="a.test", expected_state="BOOTSTRAPPING")
    a = node_factory(ready=True, name="a.test", sid="0T1", peers=["peer.test", "b.test"],
                     outgoing={"b.test": b.server_port}, propagator="peer.test")
    records = {"alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest(),
               "alice::vhost": "original.test"}
    with Peer(a) as peer:
        assert " ACK " in peer.transfer(records)
        peer.inventory({"N": records})
        root_oper = oper(a, "rootoper")
        assert "synchronization: OK" in "\n".join(root_oper.request("UDB STATUS"))
        root_oper.request("CONNECT b.test")
        b.wait_for_state("READY")
        bridge_oper = oper(b, "bridgeoper")
        bridge_oper.request("CONNECT c.test")
        c.wait_for_state("READY")
        for node in (a, b, c):
            content = (node.data_dir / "udb_N.db").read_text()
            assert all(f"{path} {value}\n" in content for path, value in records.items())
            assert all((node.data_dir / f"udb_{letter}.db").is_file() for letter in "NCISLK")
        client = c.client("visitor")
        replies = client.request("NICK alice:secret")
        assert any("You are now identified for nickname alice" in line for line in replies)
        replies = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 311 " in line and " original.test " in line for line in replies)
        start = len(client.lines)
        peer.mutation(1, "N::alice::vhost", "updated.test")
        client.wait(lambda line: " 396 alice updated.test " in line,
                    "multihop original-stream vhost update", start=start, timeout=15)
        replies = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 311 " in line and " updated.test " in line for line in replies)
        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "multihop review")
        client.wait(lambda line: "This nickname is suspended. Reason: multihop review" in line,
                    "multihop suspension", start=start, timeout=15)
        replies = client.request("MODE alice", terminator=lambda line: " 221 " in line)
        assert "r" not in next(line.rsplit(" ", 1)[-1] for line in replies if " 221 " in line)

        large_topics = {
            f"#chan{size}::topic": (letter * (size - 1)) + str(size % 10)
            for size, letter in ((510, "1"), (1024, "2"), (4000, "3"), (4096, "4"))
        }
        raw_pattern = "spam_big_" + ("k" * 3060) + "end"
        encoded_pattern = "b64%3A" + base64.b64encode(raw_pattern.encode("ascii")).decode("ascii")
        large_records = {
            "C": {f"{path} {value}" for path, value in large_topics.items()},
            "K": {f"F::{encoded_pattern}::reason SpamReason_{'5' * (4096 - 11)}"},
        }
        for sequence, (path, value) in enumerate(large_topics.items(), start=3):
            peer.mutation(sequence, f"C::{path}", value)
        peer.mutation(3 + len(large_topics), f"K::F::{encoded_pattern}::reason",
                      "SpamReason_" + ("5" * (4096 - 11)))
        wait_for_records((a, b, c), large_records)

        c_runtime_path = c.path
        c_before_restart = {
            letter: (c.data_dir / f"udb_{letter}.db").read_bytes()
            for letter in "NCISLK"
        }
        for node in (c, b, a):
            node.close()
            content = (node.data_dir / "udb_N.db").read_text()
            assert "alice::vhost updated.test\n" in content
            assert "alice::suspend multihop review\n" in content

    restarted_c = node_factory(ready=False, name="c.test", sid="0T3", path=c_runtime_path,
                              peers=["b.test", "export.test"], propagator="b.test",
                              expected_state="READY")
    assert {
        letter: (restarted_c.data_dir / f"udb_{letter}.db").read_bytes()
        for letter in "NCISLK"
    } == c_before_restart

    with Peer(restarted_c, name="export.test", sid="0E1", negotiate=False) as exporter:
        start = len(exporter.lines)
        exporter.negotiate(policy="c.test")
        inventory = exporter.wait(
            lambda line: " DB " in line and " INF " in line and " C " in line,
            "restarted leaf C inventory for downstream export",
            start=start,
        )
        round_id = inventory.split(" INF ", 1)[1].split(" ", 1)[0]
        exporter.send(f"DB {restarted_c.sid} RES {round_id} C")
        exporter.wait(lambda line: " DB " in line and f" BEGIN {round_id} C " in line,
                      "restarted leaf C snapshot BEGIN")
        exporter.wait(lambda line: " DB " in line and f" END {round_id} C " in line,
                      "restarted leaf C snapshot END")
        exported = {}
        for line in exporter.lines:
            if " DB " in line and f" PUT {round_id} C " in line:
                payload = line.split(f" PUT {round_id} C ", 1)[1]
                _, path, value = payload.split(" ", 2)
                exported[path] = value.removeprefix(":")
    for path, value in large_topics.items():
        assert exported.get(path) == value, f"restarted C truncated BIGLINES topic {path}"


def test_late_leaf_sees_live_base_and_custom_hosts_across_two_links(node_factory):
    c = node_factory(ready=False, name="privacy-c.test", sid="0T3", peers=["privacy-b.test"],
                     propagator="privacy-b.test", expected_state="BOOTSTRAPPING",
                     vhost_flood_limit=100)
    b = node_factory(ready=False, name="privacy-b.test", sid="0T2",
                     peers=["privacy-a.test", "privacy-c.test"],
                     outgoing={"privacy-c.test": c.server_port}, propagator="privacy-a.test",
                     expected_state="BOOTSTRAPPING", vhost_flood_limit=100)
    a = node_factory(ready=True, name="privacy-a.test", sid="0T1",
                     peers=["peer.test", "privacy-b.test"],
                     outgoing={"privacy-b.test": b.server_port}, propagator="peer.test",
                     vhost_flood_limit=100)
    nick_records = {
        "alice::pass": "sha256:" + hashlib.sha256(b"secret").hexdigest(),
        "alice::access": "127.0.0.0/8",
        "alice::vhost": "alice.test",
    }
    setting_records = {"encryption_key": "a1" * 32, "suffix": ".virtual"}
    with Peer(a) as upstream:
        assert " ACK " in upstream.transfer(nick_records)
        assert " ACK " in upstream.transfer(setting_records, letter="S")
        upstream.inventory({"N": nick_records, "S": setting_records})

        root_oper = oper(a, "privacy-root")
        root_oper.request("CONNECT privacy-b.test")
        b.wait_for_state("READY")

        alice = a.client("alice-setup")
        base = userhost(alice, "alice-setup")
        assert base.endswith(".virtual")
        identified = alice.request("NICK alice:secret")
        assert any("You are now identified for nickname alice" in line for line in identified)
        assert userhost(alice, "alice") == "alice.test"

        open_user = a.client("open-user")
        open_user.send("MODE open-user -x")
        assert "x" not in usermodes(open_user, "open-user")

        bridge_oper = oper(b, "privacy-bridge")
        bridge_oper.request("CONNECT privacy-c.test")
        c.wait_for_state("READY")
        observer = c.client("privacy-observer")
        assert userhost(observer, "alice") == "alice.test"
        assert userhost(observer, "open-user") == "localhost"
        observer.request("JOIN #privacy", terminator=lambda line: " 366 " in line)
        alice_start = len(observer.lines)
        alice.request("JOIN #privacy", terminator=lambda line: " 366 " in line)
        observer.wait(lambda line: line.startswith(":alice!") and " JOIN :#privacy" in line,
                      "identified user joins late leaf's channel", start=alice_start)

        open_user.send("MODE open-user +x")
        wait_for_userhost(observer, "open-user", base)

        alice.send("MODE alice -t")
        assert "t" not in usermodes(alice, "alice")
        wait_for_userhost(observer, "alice", base)
        alice.send("MODE alice +t")
        wait_for_userhost(observer, "alice", "alice.test")
        alice.send("MODE alice -x")
        assert "x" not in usermodes(alice, "alice")
        wait_for_userhost(observer, "alice", "localhost")
        alice.send("MODE alice +x")
        wait_for_userhost(observer, "alice", base)
        alice.send("MODE alice +t")
        wait_for_userhost(observer, "alice", "alice.test")

        start = len(observer.lines)
        alice.close()
        quit_line = observer.wait(lambda line: line.startswith(":alice!") and " QUIT " in line,
                                  "remote protected QUIT across A-B-C", start=start)
        assert "@alice.test QUIT " in quit_line
