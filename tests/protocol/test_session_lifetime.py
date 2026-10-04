"""Real staged receive sessions expire, release ownership and accept fresh work."""

import time
import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


@pytest.mark.parametrize("clock", ["inactivity", "absolute"])
def test_expired_session_never_publishes_and_subsequent_transaction_recovers(node_factory, clock):
    # Leave enough time for the post-expiry transaction to complete even when
    # the isolated daemon is under load; the previous 1–2s values made the
    # recovery transaction itself race its configured deadline.
    inactivity, absolute = (5, 20) if clock == "inactivity" else (20, 5)
    node = node_factory(peers=["peer.test"], propagator="peer.test",
                        settings=f'sync-inactivity-timeout {inactivity}; sync-absolute-timeout {absolute};')
    path = node.data_dir / "udb_N.db"
    before = path.read_bytes()
    records = {"alice::vhost": "alice.test"}
    with Peer(node) as peer:
        round_id, digest = peer.offer(records)
        peer.send(f"DB {node.sid} BEGIN {round_id} N expire {digest}")
        peer.send(f"DB {node.sid} PUT {round_id} N expire alice::vhost :alice.test")
        start = len(peer.lines)
        peer.barrier()
        if clock == "absolute":
            # Refresh session activity before its inactivity deadline; expiry
            # must still follow the absolute lifetime, not the last PUT.
            time.sleep(absolute / 2)
            refresh_start = len(peer.lines)
            peer.send(f"DB {node.sid} PUT {round_id} N expire alice::vhost :alice.refresh.test")
            peer.barrier()
            assert not any(" ERR PUT " in line for line in peer.lines[refresh_start:])
        reasons = (["reconciliation inventory timeout", "staged sync inactivity timeout",
                    "snapshot inactivity timeout"] if clock == "inactivity" else
                   ["reconciliation absolute timeout", "snapshot absolute timeout"])
        deadline = time.monotonic() + 15
        while not any(reason in node.log_text() for reason in reasons) and time.monotonic() < deadline:
            assert node.process.process.poll() is None, node.log_text()
            time.sleep(0.05)
        assert any(reason in node.log_text() for reason in reasons), node.log_text()
        peer.send(f"DB {node.sid} END {round_id} N expire {digest}")
        peer.barrier()
        assert path.read_bytes() == before
        assert not any(f" ACK {round_id} N expire " in line for line in peer.lines[start:])
        assert " ACK " in peer.transfer(records, txid="after-expiry")
    node.close()
    assert b"alice::vhost alice.test\n" in path.read_bytes()
