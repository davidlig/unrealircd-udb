---
name: udb-core
description: Repository map and minimal-navigation conventions for UDB code, tests, generated bundle, docs, and agentic changes.
---

# UDB Core Navigation

Use this skill only when repository ownership/location is not already obvious.

## Canonical map

- `src/udb.c` — module composition/entry source.
- `src/udb.h` — public definitions and module/protocol version constants.
- `src/udb_internal.h` — internal shared definitions, limits and runtime state.
- `src/udb_store.c.inc` — record tree, paths and persistence.
- `src/udb_config.c.inc` — `udb {}` configuration plus S/L settings effects.
- `src/udb_sync.c.inc` — HEL, propagator/authority, reconciliation and staged snapshots.
- `src/udb_operclasses.c.inc` — OCL inventories, membership, replay and OCLG view.
- `src/udb_mutation.c.inc` — `INS`, `DEL`, `DRP`, `EXP`.
- `src/udb_nicks.c.inc`, `udb_channels.c.inc`, `udb_ips.c.inc`, `udb_lines.c.inc` — block-specific schema/effects.
- `src/udb_query.c.inc` — `/UDB` and `/DBQ` diagnostics.
- `tests/` — Python unit, runtime, integration and multi-node harnesses.
- `scripts/bundle.py` — canonical deterministic bundle generator.
- `dist/udb.c` — generated; never edit manually.
- `modules.list` — distribution/module metadata.
- `README.md`, `README_ES.md`, `doc/udb_technical_{en,es}.md` — public/operator documentation.
- `AGENTS.md`, `.agentic/`, `.agents/` — agentic policy, generator and progressive skills.

## Navigation rule

1. Search for the exact symbol, command, frame, field, test name, config key or error string.
2. Open the smallest owning implementation region plus definitions it directly depends on.
3. Read callers/callees only when control flow, authority, ownership or lifetime is unclear.
4. Inspect directly relevant tests before creating new infrastructure.
5. Do not use `dist/udb.c` to understand behavior when canonical `src/` exists.
6. Do not use README/technical docs as proof of current behavior when source/tests can answer it.

Keep context local. A repository-wide read is a last resort, not a starting step.
