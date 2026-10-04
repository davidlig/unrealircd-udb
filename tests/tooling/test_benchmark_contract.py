"""Contracts for the offline hash-index benchmark tool."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest

from udb_test_support.modules import ROOT

pytestmark = pytest.mark.tooling
BENCHMARK = ROOT / "tests/perf/benchmark_hash_index.py"


def load_benchmark():
    spec = importlib.util.spec_from_file_location("udb_hash_index_benchmark", BENCHMARK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_ascii_hash_and_capacity_boundaries_match_current_index_contract():
    benchmark = load_benchmark()
    expected = 5381
    for byte in b"alice":
        expected = ((expected << 5) + expected + byte) & 0xFFFFFFFF
    assert benchmark.hash_key("Alice") == expected
    assert benchmark.hash_key("ALICE") == expected
    assert [benchmark.bucket_count(count) for count in (0, 1, 1536, 1537)] == [2048, 2048, 2048, 4096]


def test_root_parser_deduplicates_casefolded_profiles_and_keeps_first_spelling(tmp_path):
    benchmark = load_benchmark()
    snapshot = tmp_path / "udb_N.db"
    snapshot.write_text(
        "; UDB Block N - Version 1\n"
        "; Generation: 3\n"
        "Alice::vhost alice.test\n"
        "alice::access 127.0.0.1\n"
        "Bob::vhost bob.test\n",
        encoding="ascii",
    )
    assert benchmark.roots(snapshot) == ["Alice", "Bob"]


def test_measurement_distribution_is_deterministic_without_timing_thresholds():
    benchmark = load_benchmark()
    keys = [f"profile-{index}" for index in range(257)]
    first = benchmark.measure(keys, 512)
    second = benchmark.measure(keys, 512)
    for field in set(first) - {"index_build_ms"}:
        assert first[field] == second[field]
    assert first["profiles_indexed"] == len(keys)
    assert first["hash_bucket_count"] == 512
    assert 0 < first["hash_nonempty_buckets"] <= len(keys)
    assert 1 <= first["hash_probe_avg"] <= first["hash_probe_p95"] <= first["hash_probe_max"]


def test_cli_writes_machine_readable_report_for_a_small_snapshot(tmp_path):
    snapshot = tmp_path / "udb_N.db"
    snapshot.write_text("alice::vhost alice.test\nbob::vhost bob.test\n", encoding="ascii")
    output = tmp_path / "report.json"
    result = subprocess.run(
        [sys.executable, str(BENCHMARK), str(snapshot), "--output", str(output)],
        text=True,
        capture_output=True,
        timeout=10,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["fixed_2048"]["profiles_indexed"] == 2
    assert report["dynamic"]["hash_bucket_count"] == 2048
    assert "not an UnrealIRCd runtime" in report["note"]
