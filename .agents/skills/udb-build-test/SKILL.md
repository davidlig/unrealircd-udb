---
name: udb-build-test
description: Selects the smallest relevant UDB build/test command and bounded escalation path, including DB sync, OCL/OCLG, runtime and agentic tests.
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

Parsing/persistence/limits/security: `test_loader_fail_safe.py`, `test_numeric_strict.py`, `test_ipv6_and_paths.py`, `test_size_invariants.py`, `test_spamfilter_limits.py`, `test_security_hardening.py`, `test_protocol_fault_qualification.py`.

DB sync/bootstrap/readiness/convergence: `test_staged_sync_caps.py`, `test_staged_sync_ownership.py`, `test_convergence_degraded_stale.py`, `test_bootstrap_readiness_and_convergence.py`, `test_mutation_gap_recovery.py`, `test_anti_entropy.py`; expand to multihop/runtime only when the invariant requires it.

Propagator: `test_propagator_validation.py`, `test_propagator_failover.py`, `test_propagator_non_adjacent.py`, `test_propagator_runtime_failover.py`.

OCL/OCLG: `test_ocl_registry.py`, `test_ocl_rehash_replay.py`, `test_ocl_membership_multihop.py`. Start with the file owning the changed epoch, inventory, rehash/replay or topology behavior.

Nick/channel: `runtime_channel_nick.py`, `runtime_channel_modes_ins.py`, `runtime_lock_modes.py`, `runtime_schema_validation.py`.

Clone/IP: `runtime_clone_limit.py`. Runtime effects/notices: `runtime_debug_notices.py`, `staged_runtime_effects.py`.

Agentic-only changes:

```bash
python3 -m unittest .agentic/test_generate.py
python3 .agentic/generate.py --check
```

Use `./.agentic/ci-check.sh` when generator/runtime-config contracts or CI assumptions also changed.

## Escalation

Typical ceilings: static/quick 30–60s; focused Python 120–180s; runtime/integration 180–300s. Start with one relevant test. Broaden only for a shared primitive, cross-subsystem protocol change, targeted failure, explicit user request, or final pre-merge confidence.

Report exactly what ran and what remains unverified.
