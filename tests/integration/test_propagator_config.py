"""The actual UnrealIRCd config parser enforces UDB propagator host syntax."""

import pytest


pytestmark = pytest.mark.integration


def test_server_propagator_config_hostname_validation(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    node.close()
    original = node.config.read_text(encoding="ascii")
    marker = 'propagator "peer.test";'
    assert original.count(marker) == 1

    invalid = (
        "",
        "   ",
        " leading.example.net",
        "trailing.example.net ",
        "host with spaces.net",
        "host\twith\ttabs.net",
        "host\rwith\rCR.net",
        "host\nwith\nLF.net",
        ("a" * 60) + ".net",
        "invalid!host.net",
        "bad@domain.com",
        "srv:6667",
        "foo/bar",
        "nodot",
    )
    for value in invalid:
        candidate = original.replace(marker, f'propagator "{value}";', 1)
        node.config.write_text(candidate, encoding="ascii")
        with pytest.raises(RuntimeError, match="configtest failed"):
            node._run_configtest()

    valid = (
        "hub.example.net",
        "hub-1.example.org",
        "srv_01.irc.net",
        "services.local",
        ("a" * 59) + ".net",
    )
    for value in valid:
        candidate = original.replace(marker, f'propagator "{value}";', 1)
        node.config.write_text(candidate, encoding="ascii")
        node._run_configtest()
