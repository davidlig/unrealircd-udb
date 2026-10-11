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

Select one path/case with `python3 -m pytest -q <path>` and optionally `-k`.

- Native engine codecs, queue credits, source ordering, receipts, prefix delete,
  conditional expiry, disk staging and recovery: `tests/native/test_storage.py`.
  Its driver includes all production storage units, not a model store. It does
  not validate daemon business profiles or Windows. Use `--c-profile asan` or
  `UDB_NATIVE_ASAN=1` for ASan/UBSan.
- New typed wire/path syntax: `tests/unit/test_native_wire.c`.
- Strict numeric bounds: `tests/unit/test_numeric.c`.
- Native module files, six-root import, origin binding, durable mutations and
  restart: `tests/protocol/test_native_persistence.py`.
- HEL fixture: `tests/protocol/test_peer_harness.py`.
- OCL lookup: `tests/unit/test_ocl_lookup.c`; runtime OCL/OCLG inventory:
  `tests/protocol/test_ocl_inventory.py`. These are not OCL multihop proof.
- Configuration: `tests/integration/test_propagator_config.py`.

Retired text/tree/single-block-publication tests are not validation targets.
Never reintroduce compatibility helpers to make old fixtures pass. Add a focused
new-contract case when runtime ownership, convergence or a failure boundary lacks
coverage; report actual missing proof rather than implying retired coverage.

The opt-in `tests/perf/benchmark_native.py` builds the production storage engine,
uses owned scratch stores, and reports bulk build/recovery/indexed-read timings
for 1M/5M/10M rows and 128/256/512 MiB caches. Run a finite foreground timeout;
OS caches are retained, so it is not a cold-device, power-loss or IRC-throughput
benchmark. Actual Windows/MSVC/NTFS checks require that platform.

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
