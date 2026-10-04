import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol


def test_real_server_peer_negotiates_hel_and_barrier(node_factory):
    node = node_factory(peers=["peer.test"])
    with Peer(node) as peer:
        assert any(" HEL 4 " in line and " OCL" in line for line in peer.lines)
        peer.barrier()
