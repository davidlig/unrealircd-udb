"""Persisted startup candidates must be accepted as one complete six-block set."""

import socket

import pytest

from udb_test_support.peer import Peer
from udb_test_support.runtime import Wire

pytestmark = pytest.mark.recovery


def assert_registration_denied(node):
    with socket.create_connection(("127.0.0.1", node.client_port), timeout=3) as sock:
        wire = Wire(sock)
        wire.send("NICK rejected")
        wire.send("USER rejected 0 * :recovery probe")
        reply = wire.wait(lambda line: line.startswith("ERROR "), "NOT_READY rejection", timeout=3)
        assert "synchron" in reply.lower()
        assert not any(" 001 " in line for line in wire.lines)


@pytest.mark.parametrize("letter", list("NCISLK"))
@pytest.mark.parametrize("fault", ["missing", "wrong_generation"])
def test_ready_requires_six_snapshots_from_the_marker_generation(node_factory, letter, fault):
    original = {}

    def prepare(directory):
        path = directory / f"udb_{letter}.db"
        if fault == "missing":
            path.unlink()
        else:
            path.write_text(f"; UDB Block {letter} - Version 1\n; Generation: 2\n", encoding="ascii")
        original.update({p.name: p.read_bytes() for p in directory.glob("udb_*.db")})

    node = node_factory(prepare=prepare, expected_state="BOOTSTRAPPING")
    assert node.state()["ORIGIN"] == "RECOVERY"
    assert node.state()["GENERATION"] == "1"
    assert_registration_denied(node)
    node.close()
    assert {p.name: p.read_bytes() for p in node.data_dir.glob("udb_*.db")} == original


@pytest.mark.parametrize("marker", [
    "FORMAT=99\nSTATE=READY\nORIGIN=FRESH\nGENERATION=1\nLAST_SYNC=1787720000\n",
    "FORMAT=1\nSTATE=UNKNOWN\nORIGIN=FRESH\nGENERATION=1\nLAST_SYNC=1787720000\n",
    "FORMAT=1\nSTATE=READY\nORIGIN=FRESH\nGENERATION=-1\nLAST_SYNC=1787720000\n",
    "FORMAT=1\nSTATE=READY\nORIGIN=FRESH\nLAST_SYNC=1787720000\n",
])
def test_invalid_state_marker_is_preserved_and_never_grants_ready(node_factory, marker):
    def prepare(directory):
        (directory / ".udb_state").write_text(marker, encoding="ascii")

    node = node_factory(prepare=prepare, expected_state=None)
    assert_registration_denied(node)
    node.close()
    assert node.state_file.read_bytes() == marker.encode("ascii")


@pytest.mark.parametrize("duplicate", [
    "FORMAT=1", "STATE=READY", "ORIGIN=FRESH", "GENERATION=1", "LAST_SYNC=1787720000",
])
def test_duplicate_state_marker_fields_fail_closed_and_preserve_original(node_factory, duplicate):
    marker = (
        "FORMAT=1\nSTATE=READY\nORIGIN=FRESH\nGENERATION=1\nLAST_SYNC=1787720000\n"
        + duplicate + "\n"
    )

    def prepare(directory):
        (directory / ".udb_state").write_text(marker, encoding="ascii")

    node = node_factory(prepare=prepare, expected_state=None)
    assert_registration_denied(node)
    assert "Corrupted or invalid .udb_state" in node.log_text()
    node.close()
    assert node.state_file.read_bytes() == marker.encode("ascii")


@pytest.mark.parametrize("letter", list("NCISLK"))
def test_unfinished_snapshot_backup_prevents_publication_without_deleting_evidence(node_factory, letter):
    original = {}

    def prepare(directory):
        (directory / f"udb_{letter}.db.udb_previous").write_bytes(b"unfinished transaction evidence\n")
        original.update({p.name: p.read_bytes() for p in directory.iterdir()})

    node = node_factory(prepare=prepare, expected_state=None)
    assert_registration_denied(node)
    node.close()
    assert {p.name: p.read_bytes() for p in node.data_dir.iterdir() if p.name in original} == original


def test_failed_ready_marker_write_never_grants_client_admission(node_factory):
    def prepare(directory):
        # Fail only UDB's temporary state path, not UnrealIRCd's control socket.
        (directory / ".udb_state.tmp").mkdir()

    node = node_factory(ready=False, prepare=prepare, expected_state=None)
    assert_registration_denied(node)
    assert not node.state_file.exists()
    assert (node.data_dir / ".udb_state.tmp").is_dir()
    assert "Failed to persist READY state" in node.log_text()


@pytest.mark.parametrize("bad_record", [
    "malformed:::entry",
    "alice::unknown value",
    "alice::vhost::nested secret.test",
    "alice::options *18446744073709551616",
])
def test_corrupt_snapshot_preserves_disk_and_rejects_the_entire_candidate_set(node_factory, bad_record):
    original = {}

    def prepare(directory):
        path = directory / "udb_N.db"
        path.write_text(
            "; UDB Block N - Version 1\n; Generation: 1\n"
            f"alice::vhost secret.test\n{bad_record}\nbob::vhost admin.test\n",
            encoding="ascii",
        )
        original.update({p.name: p.read_bytes() for p in directory.glob("udb_*.db")})

    node = node_factory(prepare=prepare, expected_state="BOOTSTRAPPING")
    assert_registration_denied(node)
    node.close()
    assert {p.name: p.read_bytes() for p in node.data_dir.glob("udb_*.db")} == original


def test_unreadable_snapshot_fails_closed_without_overwriting_original(node_factory):
    original = b"; UDB Block N - Version 1\n; Generation: 1\nalice::vhost secret.test\n"
    path_holder = {}

    def prepare(directory):
        path = directory / "udb_N.db"
        path.write_bytes(original)
        path.chmod(0)
        path_holder["path"] = path

    node = node_factory(prepare=prepare, expected_state="BOOTSTRAPPING")
    try:
        assert_registration_denied(node)
        assert "Cannot open database file" in node.log_text() or "Failed to initialize database engine" in node.log_text()
    finally:
        node.close()
        path_holder["path"].chmod(0o600)
    assert path_holder["path"].read_bytes() == original


def test_valid_persisted_topic_sizes_load_without_truncation(node_factory):
    values = {
        f"#chan{index}::topic": ("x" * (size - 1)) + str(index)
        for index, size in enumerate([500, 1024, 2048, 4095, 4096])
    }

    def prepare(directory):
        path = directory / "udb_C.db"
        path.write_text(
            "; UDB Block C - Version 1\n; Generation: 1\n; Records: 5\n"
            + "".join(f"{key} {value}\n" for key, value in values.items()),
            encoding="ascii",
        )

    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        prepare=prepare, expected_state="READY")
    client = node.client("observer")
    assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
    key, value = next(iter(values.items()))
    reply = "\n".join(client.request(f"DBQ C::{key}"))
    # Large DBQ values can exceed IRC's line bound; this proves the loaded value
    # is present, while a subsequent snapshot rewrite proves every byte survived.
    assert value[:100] in reply
    with Peer(node) as peer:
        peer.mutation(1, "C::#loadcheck::topic", "snapshot-rewrite.test")
    node.close()
    persisted = (node.data_dir / "udb_C.db").read_bytes()
    for record_key, record_value in values.items():
        assert f"{record_key} {record_value}\n".encode("ascii") in persisted
    assert b"#loadcheck::topic snapshot-rewrite.test\n" in persisted


def test_overlong_snapshot_line_fails_closed_and_preserves_original(node_factory):
    original = (
        "; UDB Block C - Version 1\n; Generation: 1\n"
        + "#test::topic " + "y" * 13000 + "\n"
    ).encode("ascii")

    def prepare(directory):
        (directory / "udb_C.db").write_bytes(original)

    node = node_factory(ready=True, prepare=prepare, expected_state="BOOTSTRAPPING")
    assert_registration_denied(node)
    node.close()
    assert (node.data_dir / "udb_C.db").read_bytes() == original


def test_late_block_parse_failure_preserves_every_startup_snapshot(node_factory):
    original = {}

    def prepare(directory):
        path = directory / "udb_K.db"
        path.write_text(
            "; UDB Block K - Version 1\n; Generation: 1\nmalformed:::entry\n",
            encoding="ascii",
        )
        original.update({p.name: p.read_bytes() for p in directory.glob("udb_*.db")})

    node = node_factory(ready=True, prepare=prepare, expected_state="BOOTSTRAPPING")
    assert_registration_denied(node)
    node.close()
    assert {p.name: p.read_bytes() for p in node.data_dir.glob("udb_*.db")} == original


@pytest.mark.parametrize("fault", ["missing", "mixed_generation", "non_ready"])
def test_rejected_candidate_sets_never_export_persisted_k_records(node_factory, fault):
    original = {}

    def prepare(directory):
        (directory / "udb_K.db").write_text(
            "; UDB Block K - Version 1\n; Generation: 1\n"
            "Q::blocked::reason candidate must not publish this line\n",
            encoding="ascii",
        )
        if fault == "missing":
            (directory / "udb_C.db").unlink()
        elif fault == "mixed_generation":
            (directory / "udb_C.db").write_text(
                "; UDB Block C - Version 1\n; Generation: 2\n", encoding="ascii"
            )
        else:
            (directory / ".udb_state").write_text(
                "FORMAT=1\nSTATE=BOOTSTRAPPING\nORIGIN=RECOVERY\nGENERATION=1\nLAST_SYNC=0\n",
                encoding="ascii",
            )
        original.update({p.name: p.read_bytes() for p in directory.glob("udb_*.db")})

    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        prepare=prepare, expected_state="BOOTSTRAPPING")
    with Peer(node) as peer:
        start = len(peer.lines)
        peer.send(f"DB {node.sid} RES 1 K")
        peer.barrier()
        assert not any(" BEGIN " in line and " K " in line or
                       " PUT " in line and " K " in line or
                       " END " in line and " K " in line
                       for line in peer.lines[start:])
    node.close()
    assert {p.name: p.read_bytes() for p in node.data_dir.glob("udb_*.db")} == original


@pytest.mark.parametrize("letter,record", [
    ("C", "#overcapacity::modes +lllllllllllll 10 11 12 13 14 15 16 17 18 19 20 21 22"),
    ("K", f"G::{'u' * 128}@host.test persisted mask must not activate"),
])
def test_overcapacity_persisted_profile_fails_closed_and_stays_byte_preserved(
    node_factory, letter, record
):
    original = {}

    def prepare(directory):
        path = directory / f"udb_{letter}.db"
        path.write_text(
            f"; UDB Block {letter} - Version 1\n; Generation: 1\n{record}\n",
            encoding="ascii",
        )
        original[letter] = path.read_bytes()

    node = node_factory(ready=True, prepare=prepare, expected_state="BOOTSTRAPPING")
    assert_registration_denied(node)
    node.close()
    assert (node.data_dir / f"udb_{letter}.db").read_bytes() == original[letter]
