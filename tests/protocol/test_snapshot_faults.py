"""Fail-closed transaction faults retain active queries and durable snapshots."""

import time
import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


def wait_for_log(node, fragment, *, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = node.log_text()
        if fragment in text:
            return text
        assert node.process.process.poll() is None, text
        time.sleep(0.05)
    raise AssertionError(f"log event did not arrive: {fragment}\n{node.log_text()}")


@pytest.mark.parametrize("fault", [
    "begin_digest_short", "begin_digest_nonhex", "end_digest_short", "end_digest_nonhex",
    "put_wrong_round", "put_wrong_txid", "put_wrong_block", "end_wrong_block",
    "invalid_path", "invalid_schema", "numeric_overflow", "embedded_nul_encoding",
    "begin_watermark_conflict", "end_watermark_conflict", "disconnect_before_end",
])
def test_fault_does_not_publish_and_fresh_transaction_can_recover(node_factory, fault):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    original = {letter: (node.data_dir / f"udb_{letter}.db").read_bytes() for letter in "NCISLK"}
    records = {"alice::vhost": "alice.test"}
    with Peer(node) as peer:
        round_id, digest = peer.offer(records, watermark=0)
        begin_digest = "abc" if fault == "begin_digest_short" else "g" * 64 if fault == "begin_digest_nonhex" else digest
        peer.send(f"DB {node.sid} BEGIN {round_id} N faulty {begin_digest} {1 if fault == 'begin_watermark_conflict' else 0}")
        put_round = round_id + 1 if fault == "put_wrong_round" else round_id
        put_txid = "other" if fault == "put_wrong_txid" else "faulty"
        put_block = "C" if fault == "put_wrong_block" else "N"
        path, value = "alice::vhost", "alice.test"
        if fault == "invalid_path": path = "alice::::vhost"
        elif fault == "invalid_schema": path = "alice::challenge"
        elif fault == "numeric_overflow": path, value = "alice::options", "*9999999999999999999999999999999999"
        elif fault == "embedded_nul_encoding": path = "alice::%00"
        peer.send(f"DB {node.sid} PUT {put_round} {put_block} {put_txid} {path} :{value}")
        if fault == "disconnect_before_end":
            peer.send(f"SQUIT {peer.name} :interrupt transaction")
        else:
            end_digest = "abc" if fault == "end_digest_short" else "g" * 64 if fault == "end_digest_nonhex" else digest
            end_block = "C" if fault == "end_wrong_block" else "N"
            peer.send(f"DB {node.sid} END {round_id} {end_block} faulty {end_digest} {1 if fault == 'end_watermark_conflict' else 0}")
            peer.barrier()
        if fault == "invalid_schema":
            wait_for_log(node, f"Reconciliation round {round_id} aborted: invalid staged PUT payload")
        elif fault in {"wrong_end_digest", "wrong_content"}:
            wait_for_log(node, f"Reconciliation round {round_id} aborted: staged digest validation failure")
        replies = client.request("DBQ N::alice::vhost")
        assert any("Block not found" in line for line in replies), replies
        for letter, before in original.items():
            assert (node.data_dir / f"udb_{letter}.db").read_bytes() == before
        assert not any(f" ACK {round_id} N faulty " in line for line in peer.lines)
        if fault != "disconnect_before_end":
            reply = peer.transfer(records, txid="recovered", watermark=0)
            assert " ACK " in reply
            assert any("alice.test" in line for line in client.request("DBQ N::alice::vhost"))
    node.close()
    if fault == "disconnect_before_end":
        assert all((node.data_dir / f"udb_{letter}.db").read_bytes() == before
                   for letter, before in original.items())
    else:
        assert b"alice::vhost alice.test\n" in (node.data_dir / "udb_N.db").read_bytes()


@pytest.mark.parametrize("limit", ["records", "bytes"])
def test_staging_caps_accept_boundary_abort_overflow_and_allow_recovery(node_factory, limit):
    settings = 'max-staged-records 4; max-staged-bytes 1200;'
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test", settings=settings)
    original = (node.data_dir / "udb_C.db").read_bytes()
    observer = node.client("observer")
    assert any(" 381 " in line for line in observer.request("OPER testoper operpass"))

    def query(path):
        return "\n".join(observer.request(f"DBQ {path}"))

    if limit == "records":
        boundary = {"#room::topic": "topic", "#room::founder": "alice", "#room::forbid": "reason"}
        extra = ("#room::suspend", "review")
    else:
        boundary = {"#c1::topic": "A" * 590, "#c2::topic": "B" * 590}
        extra = ("#c3::topic", "C")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer(boundary, letter="C", txid="boundary")
        committed = (node.data_dir / "udb_C.db").read_bytes()
        assert committed != original
        if limit == "records":
            assert "topic" in query("C::#room::topic")
        else:
            # DBQ is returned as an IRC line and may truncate long values; durable
            # bytes below remain the exact full-value assertion.
            assert "A" * 100 in query("C::#c1::topic")
            assert "B" * 100 in query("C::#c2::topic")
        records = {**boundary, extra[0]: extra[1]}
        round_id, digest = peer.offer(records, letter="C")
        peer.send(f"DB {node.sid} BEGIN {round_id} C overflow {digest}")
        start = len(peer.lines)
        for path, value in records.items():
            peer.send(f"DB {node.sid} PUT {round_id} C overflow {path} :{value}")
        peer.wait(lambda line: " ERR " in line and " PUT " in line,
                  "staging cap overflow rejected", start=start)
        reason = "staged record limit exceeded" if limit == "records" else "staged byte limit exceeded"
        wait_for_log(node, f"Reconciliation round {round_id} aborted: {reason}")
        peer.send(f"DB {node.sid} END {round_id} C overflow {digest}")
        peer.barrier()
        assert (node.data_dir / "udb_C.db").read_bytes() == committed
        assert not any(f" ACK {round_id} C overflow " in line for line in peer.lines[start:])
        if limit == "records":
            assert "topic" in query("C::#room::topic")
            assert "Suspended reason" not in query("C::#room::suspend")
        else:
            assert "A" * 100 in query("C::#c1::topic")
            assert "B" * 100 in query("C::#c2::topic")
            assert "Block not found" in query("C::#c3::topic")
        replacement = {"#new::topic": "recovered"}
        assert " ACK " in peer.transfer(replacement, letter="C", txid="recover-cap")
    node.close()
    assert b"#new::topic recovered\n" in (node.data_dir / "udb_C.db").read_bytes()
