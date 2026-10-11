"""Final new-contract checks against the canonical production storage engine."""
from pathlib import Path
import os
import hashlib
import struct
import subprocess
import pytest
from udb_test_support.build import build_storage_driver

ROOT = Path(__file__).resolve().parents[2]

@pytest.fixture(scope="session")
def storage_driver(tmp_path_factory, request):
    output = tmp_path_factory.mktemp("native-build") / ("storage_driver.exe" if os.name == "nt" else "storage_driver")
    profile = "asan" if os.environ.get("UDB_NATIVE_ASAN") == "1" or request.config.getoption("--c-profile") == "asan" else "normal"
    return build_storage_driver(output, profile=profile)

@pytest.mark.unit
@pytest.mark.parametrize("case", ["codec", "mutations", "credits", "cancellation", "expiry", "staging", "bad-stage", "abandoned", "orphans", "split-collapse-pins", "cache-limits"])
def test_storage_contract(storage_driver, tmp_path, case):
    result = subprocess.run([str(storage_driver), case, str(tmp_path)], text=True, capture_output=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == "PASS"


def run_driver(driver, case, path):
    result = subprocess.run([str(driver), case, str(path)], text=True, capture_output=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr

@pytest.mark.recovery
@pytest.mark.parametrize("seed", ["seed", "crash", "checkpoint"])
def test_durable_recovery(storage_driver, tmp_path, seed):
    run_driver(storage_driver, seed, tmp_path)
    run_driver(storage_driver, "verify", tmp_path)

@pytest.mark.recovery
@pytest.mark.parametrize("target", ["udb_N.db", "udb_wal.db"])
def test_referenced_corruption_fails_closed(storage_driver, tmp_path, target):
    run_driver(storage_driver, "seed", tmp_path)
    file = tmp_path / "udb" / target
    with file.open("r+b") as stream:
        stream.seek(65536 + 96)
        byte = stream.read(1)
        assert byte
        stream.seek(-1, 1)
        stream.write(bytes([byte[0] ^ 1]))
    run_driver(storage_driver, "corrupt", tmp_path)

@pytest.mark.recovery
def test_incomplete_uncommitted_wal_tail(storage_driver, tmp_path):
    run_driver(storage_driver, "seed", tmp_path)
    journal = tmp_path / "udb" / "udb_wal.db"
    original_size = journal.stat().st_size
    with journal.open("ab") as stream:
        stream.write(b"uncommitted" * 31)
    run_driver(storage_driver, "verify", tmp_path)
    assert journal.stat().st_size <= original_size
    assert journal.stat().st_size % 65536 == 0
    run_driver(storage_driver, "verify", tmp_path)


@pytest.mark.recovery
@pytest.mark.parametrize("mode,recovery", [(1, "recover-old"), (2, "recover-old"),
    (3, "recover-either"), (4, "recover-old"), (5, "recover-old"), (6, "recover-new")])
def test_real_io_failure_and_process_crash(storage_driver, tmp_path, mode, recovery):
    """EIO/ENOSPC, pre-WAL, partial WAL and post-flush/pre-publication exits.

    A WAL flush failure has an uncertain physical outcome: the unacknowledged
    transaction may recover wholly, but must never publish a successful receipt.
    These are process/syscall tests, not loss of host power or device-cache proof.
    """
    run_driver(storage_driver, "seed", tmp_path)
    result = subprocess.run([str(storage_driver), f"fault-{mode}", str(tmp_path)],
                            text=True, capture_output=True, timeout=90)
    assert result.returncode == 86, result.stdout + result.stderr
    run_driver(storage_driver, recovery, tmp_path)


@pytest.mark.unit
def test_sha256_storage_domains_match_independent_vectors(storage_driver, tmp_path):
    """Pin existing on-disk/key digests, including domain NULs and byte order."""
    result = subprocess.run([str(storage_driver), "hash-vectors", str(tmp_path)],
                            text=True, capture_output=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    actual = dict(line.split() for line in result.stdout.splitlines())
    sha = lambda data: hashlib.sha256(data).digest()
    u32 = lambda number: struct.pack("<I", number)
    totals = lambda count, weight: struct.pack("<QQ", count, weight)

    def key_digest(block, components, domain, depth):
        return sha(domain + b"\0" + block + u32(depth) + b"".join(
            u32(len(component)) + component for component in components[:depth]))

    expected = {}
    for label, block, components, entity_depth in [
        ("nick", b"N", [b"alice", b"created"], 1),
        ("spamfilter", b"K", [b"f", b"CaseSensitive", b"reason"], 2),
        ("long", b"N", [b"a" * 600, b"created"], 1),
    ]:
        digest = key_digest(block, components, b"UDB4/key", len(components))
        entity = key_digest(block, components, b"UDB4/entity", entity_depth)
        expected[label + "-key"] = digest.hex()
        expected[label + "-partition"] = str((entity[0] << 4) | (entity[1] >> 4))
    partition = int(expected["nick-partition"])
    position = b"N" + struct.pack("<HH", partition, 0) + bytes(32)
    expected["empty-leaf"] = sha(b"UDB4/leaf\0" + position + totals(0, 0)).hex()
    components = u32(5) + b"alice" + u32(7) + b"created"
    length = 16 + len(components) + 8
    row = b"N\x02\x02\0" + u32(length) + u32(8) + bytes(4) + components + struct.pack("<Q", 2**64 - 1)
    expected["leaf"] = sha(b"UDB4/leaf\0" + position + u32(length) + row + totals(1, length + 128)).hex()
    weight = 16 * 1024 * 1024
    expected["branch"] = sha(b"UDB4/branch\0" + position + b"\x11" * 32 + totals(1, weight)
                             + b"\x22" * 32 + totals(1, weight) + totals(2, 2 * weight)).hex()
    page = bytes((i * 17) & 255 for i in range(65536))
    expected["page"] = sha(b"UDB4/page\0" + page[:48] + page[80:]).hex()
    expected["page-excluded-field"] = expected["page"]
    for block in b"NCISLK":
        block_byte = bytes([block])
        data = bytearray(b"UDB4/block\0" + block_byte)
        for i in range(4096):
            position = block_byte + struct.pack("<HH", i, 0) + bytes(32)
            empty = sha(b"UDB4/leaf\0" + position + totals(0, 0))
            data += struct.pack("<H", i) + totals(0, 0) + empty
        expected["block-" + chr(block)] = sha(data + totals(0, 0)).hex()
    assert actual == expected
