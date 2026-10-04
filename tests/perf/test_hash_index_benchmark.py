"""Explicit, non-threshold benchmark smoke test over a fixed large fixture."""

import json
import subprocess
import sys

import pytest

from udb_test_support.modules import ROOT

pytestmark = pytest.mark.perf
BENCHMARK = ROOT / "tests/perf/benchmark_hash_index.py"


def test_large_fixed_snapshot_reports_deterministic_capacity_and_probe_metrics(tmp_path):
    snapshot = tmp_path / "udb_N.db"
    count = 8192
    snapshot.write_text(
        "; UDB Block N - Version 1\n"
        + "".join(f"profile{index:05d}::vhost host{index}.test\n" for index in range(count)),
        encoding="ascii",
    )
    output = tmp_path / "benchmark.json"
    result = subprocess.run(
        [sys.executable, str(BENCHMARK), str(snapshot), "--output", str(output)],
        text=True,
        capture_output=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    measured = report["dynamic"]
    assert measured["profiles_indexed"] == count
    assert measured["hash_bucket_count"] == 16384
    assert 0 < measured["hash_nonempty_buckets"] <= count
    assert 1 <= measured["hash_probe_avg"] <= measured["hash_probe_p95"] <= measured["hash_probe_max"]
    assert "offline" in report["note"].lower()
