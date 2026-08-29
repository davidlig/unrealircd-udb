"""Runtime acceptance tests for the UDB consistency-closure work units.

These tests deliberately require the module compiled from this checkout.  A
runtime result from an installed or stale module cannot prove a source change.
"""

from __future__ import annotations

import pathlib
import os
import subprocess
import sys

import pytest


UDB_ROOT = pathlib.Path(__file__).resolve().parent.parent
UNREALIRCD_ROOT = UDB_ROOT.parents[3]
MODULE = UDB_ROOT / "src" / "udb.so"


def run_harness(script: str, *arguments: str) -> None:
    assert MODULE.is_file(), (
        "the UDB module built from this checkout is required; run "
        "make custommodule MODULEFILE=udb/src/udb from the UnrealIRCd source root"
    )
    result = subprocess.run(
        [sys.executable, str(UDB_ROOT / "tests" / script), *arguments],
        cwd=UDB_ROOT,
        env={**os.environ, "UDB_MODULE_PATH": str(MODULE)},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
    )
    assert result.returncode == 0, result.stdout


def test_ready_loader_rejects_non_publishable_snapshots() -> None:
    """A loader harness must use this checkout's module for candidate publication."""
    run_harness("test_loader_fail_safe.py")


@pytest.mark.parametrize(
    "fault",
    [
        "--snapshot-rename-failure",
        "--runtime-rename-failure",
        "--runtime-opt-rename-failure",
        "--runtime-del-rename-failure",
        "--runtime-drp-rename-failure",
        "--malformed-end-checksum",
    ],
)
def test_persistence_faults_do_not_acknowledge_health(fault: str) -> None:
    """END, INS, DEL, and DRP persistence failures must remain fail-closed."""
    run_harness("two_node_udb.py", fault)


def test_runtime_effects_preserve_oper_ownership_during_staged_replacement() -> None:
    """The staged effects harness proves UDB-owned live-effect revocation."""
    run_harness("staged_runtime_effects.py")
