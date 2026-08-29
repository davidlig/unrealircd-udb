---
name: udb-build-test
description: Selects and runs the smallest relevant UDB build and Python test commands for compilation, validation, regressions, runtime harnesses, convergence, and CI failures.
---

# UDB Build and Test

## Execution constraints

- Run commands synchronously in the foreground.
- Never create background tasks or subagents.
- Never poll process/task status.
- Prefer a finite GNU `timeout` for commands that may run for a long time.
- Treat timeout as a failure signal to inspect, not permission to detach or poll.
- Never rerun an unchanged passing test without a reason.
- Do not begin with the full CI suite unless explicitly required.

## Bundle determinism

- `tests/test_bundle_determinism.py` verifies reproducibility and that the
  generator never mutates `src/`.
- ASan/UBSan coverage comes from the CI `asan` matrix profile; locally, build
  UnrealIRCd with `SANITIZER=asan` in a temporary tree and run the harnesses
  with `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and
  `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`.

## Module build

From the UnrealIRCd source root where this repository is available as `src/modules/third/udb`, prefer:

```bash
make custommodule MODULEFILE=udb/src/udb
```

Do not rebuild all UnrealIRCd unless needed.

### Installed-module freshness (mandatory before runtime tests)

Runtime and integration tests load the module installed in the test
UnrealIRCd tree (`$HOME/unrealircd/modules/third/udb.so`, or
`UDB_TEST_IRCD_ROOT`/`UDB_MODULE_PATH` overrides), NOT the freshly built
`src/udb.so`. A stale installed `.so` silently validates old behavior.

After every build and before any runtime/integration test, refresh and
verify:

```bash
cp src/udb.so "$HOME/unrealircd/modules/third/udb.so"
python3 .agentic/test_runtime_module_fresh.py
```

The contract test fails with the exact copy command whenever the installed
module hash differs from the build, and skips cleanly when there is nothing
to compare (no build, or no runtime tree). `ci-check.sh` runs it as part of
the local/CI gate.

## Test matrix

### Parsing, persistence, limits, paths

Use the smallest relevant subset:
- `tests/test_loader_fail_safe.py`
- `tests/test_numeric_strict.py`
- `tests/test_ipv6_and_paths.py`
- `tests/test_size_invariants.py`
- `tests/test_spamfilter_limits.py`

### Staged sync, bootstrap, readiness, convergence

Use the smallest relevant subset:
- `tests/test_staged_sync_caps.py`
- `tests/test_staged_sync_ownership.py`
- `tests/test_convergence_degraded_stale.py`
- `tests/test_bootstrap_readiness_and_convergence.py`

Expand only when needed:
- `tests/test_multihop_mutations.py`
- `tests/test_biglines_multihop.py`
- `tests/staged_runtime_effects.py`

Changes involving readiness, bootstrap peer selection, `HEL 4`, STALE/DEGRADED recovery, or six-block reconciliation should normally include `tests/test_bootstrap_readiness_and_convergence.py`.

### Propagator

Use the relevant subset:
- `tests/test_propagator_validation.py`
- `tests/test_propagator_failover.py`
- `tests/test_propagator_non_adjacent.py`
- `tests/test_propagator_runtime_failover.py`

### Nick/channel

Use the relevant subset:
- `tests/runtime_channel_nick.py`
- `tests/runtime_channel_modes_ins.py`
- `tests/runtime_lock_modes.py`
- `tests/runtime_schema_validation.py`

### IP/clone

- `tests/runtime_clone_limit.py`

### Runtime notices/effects

- `tests/runtime_debug_notices.py`
- `tests/staged_runtime_effects.py`

## Typical finite bounds

When appropriate:
- quick/static checks: 30–60 seconds;
- focused Python test: 120–180 seconds;
- runtime/integration test: 180–300 seconds.

Use observed normal behavior to choose a sensible bound.

## Escalation

Broaden validation only if:
- a shared/core protocol primitive changed;
- a targeted failure suggests wider impact;
- the user asks for complete validation;
- final pre-merge confidence requires it.

Always report exactly what ran and what did not.
