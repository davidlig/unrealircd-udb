---
name: udb-build-test
description: Selects the smallest relevant UDB build/test command and bounded escalation path, including DB sync, OCL/OCLG, runtime and agentic tests.
---

# UDB Build and Test

## Execution

Run synchronously in the foreground. No background jobs, subagents, status polling, or repeated unchanged tests. Use a finite timeout for commands that can hang.

## Setup and build

The authoritative suite is cmocka + pytest; see `tests/README.md` for prerequisites
and environment overrides. From the UDB checkout:

```bash
python3 -m venv tests/.build/venv
. tests/.build/venv/bin/activate
python3 -m pip install -r tests/requirements.txt
# Only for canonical C cases: requires CMake, a compiler and configured daemon headers.
python3 tests/support/bootstrap_cmocka.py
export UNREALIRCD_SOURCE=/path/to/configured/unrealircd-source
make -C "$UNREALIRCD_SOURCE" custommodule MODULEFILE=udb/src/udb
```

Tooling-only tests do not need cmocka, a daemon build or a runtime. Do not rebuild
all UnrealIRCd unless evidence requires it.

## Runtime module freshness

After building, select and check the checkout module from the UDB repository root:

```bash
export UDB_MODULE_PATH="$PWD/src/udb.so"
python3 .agentic/test_runtime_module_fresh.py
```

Honor `UDB_MODULE_PATH` and `UDB_TEST_IRCD_ROOT` (installed test daemon root,
default `$HOME/unrealircd`). Fixtures copy the selected module into disposable
loopback nodes and mount the installed runtime read-only. Never overwrite a
user's installed module or restart existing servers for tests. Missing mandatory
runtime/C capabilities must fail, not skip. `--c-profile asan` instruments C test
executables only; runtime sanitizers require a separately instrumented daemon/module.

## Focused test map

Select one file or case with `python3 -m pytest -q -m '<family>' <path>`;
use `-k` for an affected scenario. Canonical C cases are collected by pytest.

- Parsing/schema/store: `tests/unit/test_numeric.c`, `tests/unit/test_schema.c`, `tests/unit/test_store.c` (`unit`).
- Limits/paths/framing: `tests/protocol/test_size_limits.py`, `tests/protocol/test_ipv6_paths.py`, `tests/protocol/test_numeric_frames.py` (`protocol`).
- DB staging/persistence/recovery: `tests/unit/test_staging.c`, `tests/protocol/test_db_snapshots.py`, `tests/protocol/test_snapshot_ownership.py`, `tests/protocol/test_snapshot_persistence.py`, `tests/protocol/test_snapshot_faults.py`, `tests/recovery/test_startup_fail_closed.py`.
- Mutation/convergence/authority: `tests/protocol/test_live_mutations.py`, `tests/protocol/test_multinode.py`, `tests/protocol/test_session_lifetime.py` (`protocol`). Expand topology only for the affected invariant.
- Propagator: `tests/integration/test_propagator_config.py`, `tests/protocol/test_propagator_values.py`.
- OCL/OCLG: `tests/unit/test_ocl_lookup.c`, `tests/protocol/test_ocl_inventory.py`; existing DB/host multihop tests do not prove OCL multihop coverage.
- Nick/host/effect ownership: `tests/integration/test_nick_identity.py`, `tests/integration/test_host_privacy.py`, `tests/model/test_nick_effect_ownership.py`.
- Channel JOIN/ranks/topic: `tests/integration/test_channel_profiles.py`.
- K lines/expiry: `tests/integration/test_k_line_server_bans.py`, `tests/integration/test_k_line_expiry.py`.
- Offline hash benchmark: `tests/perf/test_hash_index_benchmark.py`; opt in with `-m perf` or `--run-perf`.

If no current case covers the requested behavior, report the gap and select a
source-grounded regression within authorized scope; do not infer coverage from
retired script names.

Agentic-only changes (`tooling`):

```bash
python3 -m pytest -q -m tooling tests/tooling/test_agentic.py tests/tooling/test_ci.py
python3 .agentic/generate.py --check
```

`./.agentic/ci-check.sh` runs these contracts and generator sync without a runtime.
Use `python3 -m pytest -q -m tooling` only when shared tooling also changed.

## Escalation

Typical ceilings: static/quick 30–60s; focused Python 120–180s; runtime/integration 180–300s. Start with one relevant test. Broaden only for a shared primitive, cross-subsystem protocol change, targeted failure, explicit user request, or final pre-merge confidence.

Report exactly what ran and what remains unverified.
