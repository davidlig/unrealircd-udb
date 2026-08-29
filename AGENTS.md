# unrealircd-udb — Shared agent instructions

This file is intentionally shared by Antigravity CLI and OpenCode.

UDB 4 is a C module for UnrealIRCd 6 with Python unit, integration, runtime, and multi-node tests.

## 1. Mandatory anti-polling and token-efficiency policy

The primary operational goal of this agentic configuration is to prevent CLI quota/context waste while waiting for asynchronous work.

All agents MUST follow these rules:

- Do not create background tasks.
- Do not invoke background subagents.
- Do not use `invoke_subagent`, `define_subagent`, `manage_task`, `ManageTask`, `/btw`, or equivalent asynchronous delegation.
- Do not repeatedly check task/process status.
- Do not use `&`, `nohup`, `disown`, `tmux`, `screen`, or detached shell execution.
- Run shell commands synchronously in the foreground.
- Prefer small targeted commands over broad long-running commands.
- Use a finite foreground `timeout` for potentially long commands when practical.
- If a command times out, inspect the output already returned. Do not detach it and do not poll it.
- Do not rerun an unchanged passing test without a concrete reason.
- Do not run the complete CI suite after every edit.

### Antigravity enforcement

Project agents under `.agents/agents/` use:

```yaml
mainAgent: true
subagent: false
```

Their explicit `tools` allowlist excludes delegation and background-task tools. `subagent: false` prevents these roles from being invoked as child agents; the tool allowlist prevents them from launching children.

Select specialists manually with `/agents`.

### OpenCode enforcement

Project agents under `.opencode/agents/` use:

```yaml
mode: primary
permission:
  task: deny
```

The `task` denial prevents the primary agent from launching OpenCode child sessions. `mode: primary` keeps the role manually selectable without advertising it as a subagent.

## 2. Shared knowledge vs CLI-specific wrappers

Shared project knowledge lives in:

- this `AGENTS.md`;
- `.agents/skills/*/SKILL.md`.

Both Antigravity and OpenCode use the same project skills.

Do not duplicate those skills under `.opencode/skills/`.

CLI-specific agent wrappers live in:

- Antigravity: `.agents/agents/<agent>/agent.md`
- OpenCode: `.opencode/agents/<agent>.md`

Keep domain knowledge in shared skills whenever possible. Keep wrappers small.

## 3. Agent routing

Four manually selectable roles are provided in both CLIs.

### `udb-developer`

Default role for:
- implementation;
- debugging;
- maintenance;
- refactoring;
- features;
- targeted build/test work.

### `udb-sync-specialist`

Use for:
- `HEL 4`;
- S2S `DB` protocol;
- staged synchronization;
- propagators;
- bootstrap;
- readiness;
- reconciliation;
- STALE/DEGRADED recovery;
- failover/failback;
- multi-node convergence.

### `udb-reviewer`

Use for:
- commit/PR review;
- correctness/security review;
- memory/lifetime audit;
- regression analysis.

The reviewer is strictly read-only and has neither edit nor shell capability.

Provide the diff or patch in conversation context. Use `udb-test-engineer` or another shell-capable primary role to capture Git output or run validation requested by the reviewer.

### `udb-test-engineer`

Use for:
- regression tests;
- Python harnesses;
- integration/runtime tests;
- multi-node tests;
- CI/test failures.

## 4. Shared skills

Reusable procedures are under `.agents/skills/`:

- `udb-build-test`
- `udb-sync-protocol`
- `udb-security`
- `udb-code-review`
- `udb-bundle-release`

Skills are intended to be loaded on demand rather than copied into every agent prompt.

## 5. Repository map

- `src/udb.c` — module composition/entry source.
- `src/udb.h` — public definitions.
- `src/udb_internal.h` — internal shared definitions.
- `src/udb_*.c.inc` — implementation units.
- `tests/` — Python tests and runtime/multi-node harnesses.
- `scripts/bundle.py` — canonical bundle generator.
- `dist/udb.c` — generated bundled source; NEVER edit manually.
- `modules.list` — module/distribution metadata.
- `doc/` — technical documentation.
- `.github/workflows/ci.yml` — canonical CI sequence.

Before a non-trivial change, locate the owning implementation unit with search. Avoid loading or rereading unrelated large files.

## 6. Core UDB invariants

Unless the user explicitly requests a redesign, preserve:

- fail-closed parsing and validation;
- staged synchronization atomicity;
- no mutation of active state before a staged transaction validates and commits;
- confirmed `HEL 4` capability before using a peer for UDB synchronization;
- authorized direct-peer synchronization sources;
- propagator authority and deterministic failover/failback;
- single-source bootstrap when no propagator policy exists;
- explicit database readiness gating;
- complete convergence of all six blocks `N`, `C`, `I`, `S`, `L`, `K` before recovery to healthy state;
- deterministic conflict/convergence behavior;
- strict record/path/value/component/S2S-frame limits;
- safe UnrealIRCd `Client *` lifetime and module-resource ownership.

Never weaken a safety invariant merely to make a test pass.

## 7. Editing discipline

- Inspect `git status --short` before editing.
- Make the smallest coherent diff.
- Do not refactor, rename, reorder, or reformat unrelated code.
- Follow `.clang-format`: tabs, width 4, Allman braces, 120-column limit.
- Never overwrite unrelated working-tree changes.
- Never use destructive Git commands unless explicitly requested.
- Do not commit, push, merge, force-push, or rewrite history unless explicitly requested.

For every C change consider:
- NULL handling;
- ownership and cleanup;
- module unload/reload lifetime;
- callbacks/events;
- stale `Client *`;
- integer overflow/underflow;
- signed/unsigned conversion;
- bounds and NUL termination;
- silent truncation;
- malformed/replayed/out-of-state protocol input;
- rollback;
- persistence failures;
- multi-node ordering and convergence.

## 8. Generated artifacts

When canonical source changes affect the distribution:

1. edit canonical files under `src/`;
2. run `scripts/format-sources` only if formatting is intended (the bundle never formats);
3. run `python3 scripts/bundle.py` synchronously (it is read-only over `src/`);
4. inspect the resulting diff;
5. run `python3 scripts/bundle.py --check && git diff --check`.

Never manually edit `dist/udb.c` to make generated output or CI pass.

Update `README.md` and/or `doc/` when public configuration, protocol semantics, limits, security behavior, or operator-visible behavior changes.

## 9. Validation strategy

Start with the smallest relevant test(s).

Expand validation only when:
- a shared/core primitive changed;
- protocol/convergence behavior spans several subsystems;
- targeted failures indicate broader impact;
- the user requests complete validation;
- final pre-merge confidence reasonably requires it.

The detailed test matrix is in the `udb-build-test` skill.
