"""Windows-only NTFS contracts for the production storage engine."""

import os
from pathlib import Path
import subprocess
import time

import pytest

from udb_test_support.build import build_storage_driver


pytestmark = pytest.mark.skipif(os.name != "nt", reason="requires Win32 handles and local NTFS")


@pytest.fixture(scope="module")
def storage_driver(tmp_path_factory):
    return build_storage_driver(tmp_path_factory.mktemp("win-native-build") / "storage_driver.exe")


def run(driver: Path, case: str, path: Path):
    result = subprocess.run([str(driver), case, str(path)], text=True, capture_output=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == "PASS"


def test_unicode_paths_and_positioned_io_above_4gib(storage_driver, tmp_path):
    store = tmp_path / "espacio con ñ y 東京" / "datos"
    store.mkdir(parents=True)
    run(storage_driver, "win-offset", store)


def test_insecure_store_directory_is_rejected(storage_driver, tmp_path):
    run(storage_driver, "win-insecure-directory", tmp_path)


def test_insecure_store_file_is_rejected(storage_driver, tmp_path):
    run(storage_driver, "win-insecure-file", tmp_path)


def test_reparse_point_store_directory_is_rejected(storage_driver, tmp_path):
    target = tmp_path / "junction-target"
    target.mkdir()
    store = tmp_path / "junction-store"
    store.mkdir()
    junction = store / "udb"
    result = subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(junction), str(target)],
                            text=True, capture_output=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    try:
        run(storage_driver, "win-reject-store", store)
    finally:
        junction.rmdir()


def test_hardlinked_store_file_is_rejected(storage_driver, tmp_path):
    run(storage_driver, "win-hardlink", tmp_path)


def test_second_process_cannot_open_the_same_store(storage_driver, tmp_path):
    holder = subprocess.Popen([str(storage_driver), "win-hold", str(tmp_path), "6000"],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        time.sleep(0.35)
        assert holder.poll() is None, (holder.stdout.read() if holder.stdout else "")
        run(storage_driver, "win-lock-probe", tmp_path)
        output, errors = holder.communicate(timeout=10)
        assert holder.returncode == 0, output + errors
        assert output.splitlines() == ["READY", "PASS"]
    finally:
        if holder.poll() is None:
            holder.kill()
            holder.communicate(timeout=5)


def test_engine_close_releases_handles_and_worker_threads(storage_driver, tmp_path):
    run(storage_driver, "win-close-cycles", tmp_path)


@pytest.mark.parametrize("slot", ["udb_meta0.db", "udb_meta1.db"])
def test_one_corrupt_metadata_slot_recovers_from_its_peer(storage_driver, tmp_path, slot):
    run(storage_driver, "seed", tmp_path)
    metadata = tmp_path / "udb" / slot
    with metadata.open("r+b") as stream:
        stream.seek(96)
        byte = stream.read(1)
        assert byte
        stream.seek(-1, 1)
        stream.write(bytes([byte[0] ^ 0x80]))
    run(storage_driver, "verify", tmp_path)


def test_both_corrupt_metadata_slots_fail_closed(storage_driver, tmp_path):
    run(storage_driver, "seed", tmp_path)
    for slot in ("udb_meta0.db", "udb_meta1.db"):
        metadata = tmp_path / "udb" / slot
        with metadata.open("r+b") as stream:
            stream.seek(96)
            byte = stream.read(1)
            assert byte
            stream.seek(-1, 1)
            stream.write(bytes([byte[0] ^ 0x80]))
    run(storage_driver, "corrupt", tmp_path)
