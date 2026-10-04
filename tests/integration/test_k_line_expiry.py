"""Current runtime behavior for absolute Block K Q-line expiry."""

import socket
import time

import pytest

from udb_test_support.runtime import Wire


pytestmark = pytest.mark.integration


def test_persisted_qline_is_enforced_until_authoritative_expiry(node_factory):
    expires = int(time.time()) + 10

    def seed_k_snapshot(data_dir):
        block = data_dir / "udb_K.db"
        block.write_text(
            "; UDB Block K - Version 1\n"
            "; Generation: 1\n"
            f"Q::expiring::expires *{expires}\n"
            "Q::expiring::reason temporary current-behavior test\n",
            encoding="ascii",
        )

    node = node_factory(ready=True, prepare=seed_k_snapshot)
    block = node.data_dir / "udb_K.db"
    assert b"Q::expiring::reason temporary current-behavior test\n" in block.read_bytes()
    assert int(time.time()) < expires, "the isolated daemon took too long to start the expiry fixture"

    probe = Wire(socket.create_connection(("127.0.0.1", node.client_port), timeout=5))
    try:
        probe.send("PROTOCTL NAMESX")
        probe.send("NICK expiring")
        probe.send("USER expiring 0 * :expiry probe")
        rejection = probe.wait(
            lambda line: " 432 " in line or " 001 expiring " in line,
            "runtime Q-line decision for an expiring nickname",
        )
        assert " 432 " in rejection, f"active persisted Q-line did not reject its nickname: {rejection}"
    finally:
        probe.close()

    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        persisted = block.read_bytes()
        if b"Q::expiring::" not in persisted:
            break
        if node.process.process.poll() is not None:
            pytest.fail(f"isolated daemon exited before K expiry completed:\n{node.log_text()}")
        time.sleep(0.1)
    else:
        pytest.fail(f"authoritative expiry did not delete the K profile:\n{block.read_text(encoding='ascii')}")

    accepted = node.client("expiring")
    assert any(" 001 expiring " in line for line in accepted.lines)
