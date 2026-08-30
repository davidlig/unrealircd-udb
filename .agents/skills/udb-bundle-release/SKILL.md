---
name: udb-bundle-release
description: Keeps canonical UDB source, generated dist/udb.c, formatting, metadata, and public docs synchronized after relevant source changes.
---

# UDB Bundle and Release Hygiene

`src/` is canonical; `dist/udb.c` is generated and must never be edited manually.

When canonical source changes affect the distribution:

```bash
python3 scripts/bundle.py
python3 scripts/bundle.py --check
git diff --check
```

Formatting is separate (`scripts/format-sources`) and should run only when formatting is intended; regenerate afterward.

Update `README.md`, `doc/`, or `modules.list` only when the change affects their documented surface: configuration, protocol/operator semantics, auth/privileges, limits, install/build, persistence/sync guarantees, or distribution metadata.

Inspect the generated diff and reject unrelated churn or temporary artifacts.
