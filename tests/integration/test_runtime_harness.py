"""Real-daemon smoke tests for the shared replacement integration fixtures."""

import pytest

pytestmark = pytest.mark.integration


def test_seeded_ready_state_and_real_irc_registration(node_factory):
    node = node_factory(ready=True)
    assert node.wait_for_state("READY")["GENERATION"] == "1"
    client = node.client("observer")
    assert any(" 001 observer " in line for line in client.lines)


def test_live_wire_answers_ping_and_uses_fresh_line_offsets(node_factory):
    node = node_factory(ready=True)
    client = node.client("observer")
    start = len(client.lines)
    client.send("PING :udb-test-barrier")
    assert client.wait(
        lambda line: line.endswith(" PONG udb-test.test :udb-test-barrier"),
        "server PONG to live test wire", timeout=2, start=start,
    )

    start = len(client.lines)
    with pytest.raises(TimeoutError):
        client.wait(
            lambda line: "impossible-udb-reply" in line,
            "unrequested IRC reply", timeout=0.02, start=start,
        )


def test_fresh_standalone_bootstrap_persists_all_six_blocks(node_factory):
    node = node_factory(ready=False)

    state = node.wait_for_state("READY")
    assert int(state["GENERATION"]) >= 1
    for letter in "NCISLK":
        block = node.data_dir / f"udb_{letter}.db"
        assert block.is_file(), f"fresh startup did not persist block {letter}"
        assert block.read_text(encoding="ascii").startswith(
            f"; UDB Block {letter} - Version 1\n"
        )


def test_ready_persistence_survives_clean_daemon_restart(node_factory):
    first = node_factory(ready=False)
    state = first.wait_for_state("READY")
    generation = state["GENERATION"]
    persisted = {
        letter: (first.data_dir / f"udb_{letter}.db").read_bytes()
        for letter in "NCISLK"
    }
    runtime_path = first.path
    first.close()

    restarted = node_factory(ready=False, path=runtime_path)
    recovered = restarted.wait_for_state("READY")
    assert recovered["GENERATION"] == generation
    for letter, contents in persisted.items():
        assert (restarted.data_dir / f"udb_{letter}.db").read_bytes() == contents
