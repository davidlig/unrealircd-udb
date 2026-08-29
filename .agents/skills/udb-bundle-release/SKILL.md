---
name: udb-bundle-release
description: Keeps UDB canonical source, generated dist/udb.c, modules.list, documentation, formatting, and release-facing artifacts synchronized after changes.
---

# UDB Bundle and Release Hygiene

## Canonical source

`src/` is canonical.

`dist/udb.c` is generated. Never manually edit it to fix bundle output or CI.

## After canonical source changes

When bundle output is affected, run synchronously:

```bash
python3 scripts/bundle.py
git diff --check
```

`scripts/bundle.py` is a deterministic, read-only amalgamator: it never formats
and never mutates `src/`. Formatting is an explicit separate step
(`scripts/format-sources`) and must be followed by regenerating the bundle.
Verify without writing anything with `python3 scripts/bundle.py --check`.

Inspect the generated diff and ensure it reflects only intended canonical changes.

## Documentation

Update `README.md` and/or `doc/` when behavior changes:
- configuration;
- protocol/operator-visible semantics;
- authentication/privileges;
- supported settings/options;
- limits;
- installation/build procedure;
- persistence/synchronization guarantees.

Avoid documentation churn for purely internal behavior with no documented impact.

## Completion check

Verify:
- source and generated bundle agree;
- `modules.list` is synchronized when relevant;
- unrelated formatting did not change;
- temporary artifacts are absent;
- `git diff --check` passes;
- targeted tests are reported.
