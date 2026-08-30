# unrealircd-udb — Project instructions

UDB 4 is a C module for UnrealIRCd 6. Canonical code lives under `src/`; `dist/udb.c` is generated.

## Execution and token budget

- Work synchronously in the foreground. Never launch subagents, background tasks, detached commands, or polling loops.
- Do not use `task`, `invoke_subagent`, `define_subagent`, `manage_task`, `ManageTask`, `/btw`, `/teamwork-preview`, or equivalents.
- For a potentially long command, use one finite foreground timeout when practical. If it expires, inspect the returned output once; do not poll.
- Use this loop: locate -> read the minimum evidence -> change/review -> run targeted validation -> stop.
- Start from the user-named diff, path, symbol, failure, or test. Search before opening large files. Expand context only for a concrete dependency or invariant.
- Do not scan the whole repository, reread unchanged files, or rerun an unchanged passing test without a reason.
- Skills are progressive context. Load only the skill whose trigger matches the task; never load several "just in case".

## Editing discipline

- Before edits, inspect `git status --short` once when shell access is available.
- Make the smallest coherent diff. No unrelated refactors, renames, formatting, cleanup, or legacy compatibility unless explicitly requested.
- Preserve unrelated working-tree changes. Never use destructive Git operations or commit/push/merge unless explicitly requested.
- Edit canonical `src/` files, never `dist/udb.c`. Regenerate the bundle only when canonical source changes require it.
- Update README/docs only for public configuration, protocol, limits, security, installation, or operator-visible behavior changes.

## UDB correctness invariants

Unless the user explicitly asks for a redesign, preserve all of these:

- fail-closed parsing, validation, authorization, and bounds checks;
- staged synchronization remains isolated from active state until validation and commit succeed;
- synchronization requires confirmed `HEL 4` capability and an authorized eligible direct peer;
- bootstrap, propagator authority, failover/failback, and conflict resolution remain deterministic;
- readiness/recovery requires complete convergence of all six blocks `N`, `C`, `I`, `S`, `L`, `K`;
- malformed, duplicate, replayed, unsolicited, oversized, partial, or out-of-state protocol input cannot advance state incorrectly;
- C ownership, cleanup, module unload/reload, callback/event lifetime, `Client *` lifetime, integer arithmetic, NUL termination, and truncation remain safe;
- persistence failures never silently replace valid active state with partial or unvalidated data.

Never weaken an invariant merely to make a test pass.

## Validation

- Start with the smallest relevant check described by `udb-build-test`.
- Broaden only when a shared primitive changed, protocol/convergence crosses subsystems, targeted evidence requires it, or the user explicitly asks for full validation.
- Before runtime/integration tests, rebuild and refresh the installed module as described by `udb-build-test`.
- After source changes that affect distribution, use `udb-bundle-release`.
- Final output should be compact: changed/found, validation run, and any concrete residual risk or unverified item.

When the user writes in Spanish, answer in Spanish. Keep source identifiers and repository terminology unchanged.
