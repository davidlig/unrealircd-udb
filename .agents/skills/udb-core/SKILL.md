---
name: udb-core
description: Repository map and minimal-navigation conventions for UDB code changes. Use when locating ownership of a symbol or choosing canonical files.
---

# UDB Core Navigation

Use this skill only when repository ownership/location is not already obvious.

## Canonical map

- `src/udb.c` — module composition/entry source.
- `src/udb.h` — public definitions.
- `src/udb_internal.h` — internal shared definitions/state.
- `src/udb_*.c.inc` — implementation units; prefer the owning unit over broad reads.
- `tests/` — Python unit, runtime, integration, and multi-node harnesses.
- `scripts/bundle.py` — canonical deterministic bundle generator.
- `dist/udb.c` — generated; never edit manually.
- `modules.list` — distribution/module metadata.
- `doc/` and `README.md` — public/operator documentation.

## Navigation rule

1. Search for the exact symbol, command, frame, field, test name, or error string.
2. Open the smallest owning implementation region plus definitions it directly depends on.
3. Read callers/callees only when control flow or ownership is unclear.
4. Inspect the directly relevant tests before creating new infrastructure.
5. Do not open `dist/udb.c` to understand behavior when canonical `src/` exists.

Keep context local. A repository-wide read is a last resort, not a starting step.
