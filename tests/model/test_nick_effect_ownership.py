"""Independent projection model compared with one live identity trace."""

import hashlib

import pytest

from udb_test_support.peer import Peer


pytestmark = [pytest.mark.model, pytest.mark.integration]


class NickEffectProjection:
    """Small contract model of identity, newly owned modes, and vhost restore."""

    def __init__(self, modes, host):
        self.modes = set(modes)
        self.host = host
        self.owned_modes = set()
        self.identity = False
        self.original_host = host
        self.applied_host = None
        self.owns_host = False

    def authenticate(self, desired_modes, desired_host):
        self.identity = True
        self.owned_modes = set(desired_modes) - self.modes
        self.modes.update(desired_modes)
        self.modes.add("r")
        if desired_host and desired_host != self.host:
            self.applied_host = desired_host
            self.owns_host = True
            self.host = desired_host
            self.modes.update({"t", "x"})

    def suspend(self):
        self.identity = False
        self.modes.difference_update(self.owned_modes)
        self.modes.discard("r")
        self.owned_modes.clear()
        if self.owns_host and self.host == self.applied_host:
            self.host = self.original_host
            self.modes.discard("t")
            self.modes.add("x")
        self.applied_host = None
        self.owns_host = False


def _modes(client, nickname):
    replies = client.request(f"MODE {nickname}", terminator=lambda line: " 221 " in line)
    line = next(line for line in replies if " 221 " in line)
    return set(line.rsplit(" ", 1)[-1].lstrip(":+"))


def _host(client, nickname):
    replies = client.request(f"WHOIS {nickname}", terminator=lambda line: " 318 " in line)
    line = next(line for line in replies if " 311 " in line)
    return line.split()[5]


def test_same_mode_overlap_survives_suspend_and_owned_identity_is_revoked(node_factory):
    password = "model-secret"
    records = {
        "alice::pass": "sha256:" + hashlib.sha256(password.encode()).hexdigest(),
        "alice::vhost": "alice.model.test",
        "alice::modes": "+i",
    }
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        assert " ACK " in peer.transfer(records)
        client = node.client("visitor")
        client.request("MODE visitor +i")

        initial_modes = _modes(client, "visitor")
        initial_host = _host(client, "visitor")
        assert "i" in initial_modes
        model = NickEffectProjection(initial_modes, initial_host)

        model.authenticate({"i"}, "alice.model.test")
        authenticated = client.request(f"NICK alice:{password}")
        assert any(line.endswith(" NICK :alice") for line in authenticated), authenticated
        assert "r" in _modes(client, "alice")
        assert _modes(client, "alice") == model.modes
        assert _host(client, "alice") == model.host
        assert model.identity

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "modelled suspension")
        client.wait(
            lambda line: "This nickname is suspended. Reason: modelled suspension" in line,
            "suspension effect reconciliation",
            start=start,
        )
        model.suspend()

        assert _modes(client, "alice") == model.modes
        assert "i" in model.modes
        assert "r" not in model.modes
        assert _host(client, "alice") == model.host == initial_host
        assert not model.identity
