---
name: udb-build-test
description: Selects the smallest relevant UDB build/test command and bounded escalation path. Use for compilation, regressions, runtime tests, or CI failures.
---

# UDB Build and Test

## Execution

Run synchronously in the foreground. No background jobs, subagents, status polling, or repeated unchanged tests. Use a finite timeout for commands that can hang.

## Build

From the UnrealIRCd source root where this repo is `src/modules/third/udb`:

```bash
make custommodule MODULEFILE=udb/src/udb
```

Do not rebuild all UnrealIRCd unless evidence requires it.

## Runtime module freshness

Runtime/integration tests load the installed test module, not necessarily the fresh build. After a build and before runtime/integration tests:

```bash
cp src/udb.so "$HOME/unrealircd/modules/third/udb.so"
python3 .agentic/test_runtime_module_fresh.py
```

Honor `UDB_TEST_IRCD_ROOT` / `UDB_MODULE_PATH` overrides when present.

## Focused test map

Parsing/persistence/limits: `test_loader_fail_safe.py`, `test_numeric_strict.py`, `test_ipv6_and_paths.py`, `test_size_invariants.py`, `test_spamfilter_limits.py`.

Sync/bootstrap/readiness/convergence: `test_staged_sync_caps.py`, `test_staged_sync_ownership.py`, `test_convergence_degraded_stale.py`, `test_bootstrap_readiness_and_convergence.py`; expand to multihop/runtime only when the changed invariant requires it.

Propagator: `test_propagator_validation.py`, `test_propagator_failover.py`, `test_propagator_non_adjacent.py`, `test_propagator_runtime_failover.py`.

Nick/channel: `runtime_channel_nick.py`, `runtime_channel_modes_ins.py`, `runtime_lock_modes.py`, `runtime_schema_validation.py`.

Clone/IP: `runtime_clone_limit.py`. Runtime effects/notices: `runtime_debug_notices.py`, `staged_runtime_effects.py`.

## Escalation

Typical ceilings: static/quick 30–60s; focused Python 120–180s; runtime/integration 180–300s. Start with one relevant test. Broaden only for a shared primitive, cross-subsystem protocol change, targeted failure, explicit user request, or final pre-merge confidence.

Report exactly what ran and what remains unverified.
