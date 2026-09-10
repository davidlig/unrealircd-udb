---
name: udb-documentation
description: Evidence-driven workflow for keeping UDB README and technical documentation in English and Spanish accurate and semantically synchronized.
---

# UDB Documentation

Use for `README.md`, `README_ES.md`, `doc/udb_technical_en.md`, or `doc/udb_technical_es.md` changes.

## Source hierarchy

1. Canonical `src/` implementation.
2. Executable tests and CI for demonstrated behavior.
3. Generated `dist/udb.c` only as a consistency check.
4. Existing documentation only as wording/context, never as proof.

## Evidence map

- version/module constants and hard limits: `src/udb.h`, `src/udb_internal.h`;
- database format/persistence: `udb_store.c.inc`, `udb_core.c.inc`;
- config and S/L behavior: `udb_config.c.inc`;
- HEL/propagator/readiness/reconciliation: `udb_sync.c.inc`;
- live writes: `udb_mutation.c.inc`;
- OCL/OCLG: `udb_operclasses.c.inc`;
- N/C/I/K schemas/effects: owning block units plus `udb_effects.c.inc`;
- operator diagnostics: `udb_query.c.inc`;
- distribution/version compatibility: `scripts/bundle.py`, `modules.list`, CI.

## Writing rules

- Keep README EN/ES at the same abstraction level and technical EN/ES with the same semantics/coverage.
- Do not invent defaults, ranges, protocol grammar, security guarantees or runtime consumers. If code does not prove a behavior, state the uncertainty or omit the claim.
- Preserve implementation limitations that are operator-relevant; distinguish parsed/stored settings from settings actually consumed at runtime.
- Treat OCL/OCLG as runtime-only and distinct from the six persistent DB blocks.
- Document snapshot sync as hop-by-hop and live mutations as multihop only with per-hop authority enforcement.
- Mention security-sensitive diagnostic behavior only after verifying the concrete redaction path.

## Validation

For docs-only changes, no module build is required unless a claim cannot otherwise be verified. Check links/code fences, paired-language section coverage, command/config spelling, and `git diff --check` when shell access is available.
