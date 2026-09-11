---
name: udb-bundle-release
description: Keeps canonical UDB source, generated dist/udb.c, metadata, public docs, and release-facing behavior synchronized after relevant changes.
---

# UDB Bundle and Release Hygiene

`src/` is canonical; `dist/udb.c` is generated and must never be edited manually.

When canonical source changes affect the distribution:

```bash
scripts/format-sources.sh
python3 scripts/bundle.py
python3 scripts/bundle.py --check
git diff --check
```

Formatting is a mandatory pre-generation step. Inspect any canonical-source changes made by `scripts/format-sources.sh` before accepting the regenerated bundle.

Update public documentation only when the change affects its surface: configuration, protocol/operator semantics, auth/privileges, limits, install/build, persistence/sync/OCL guarantees, diagnostics or distribution metadata.

When docs change, keep `README.md` with `README_ES.md`, and `doc/udb_technical_en.md` with `doc/udb_technical_es.md`, semantically aligned. Verify factual claims against canonical source/tests rather than copying stale text between languages.

Agentic-only changes do not require regenerating `dist/udb.c`; regenerate `.agents`/`.opencode`/`.codex` through `.agentic/generate.py` instead.

Inspect generated diffs and reject unrelated churn or temporary artifacts.
