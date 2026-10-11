"""Final new-contract checks against the canonical production storage engine."""
from pathlib import Path
import os
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
