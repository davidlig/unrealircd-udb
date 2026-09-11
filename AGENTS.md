# unrealircd-udb — Project instructions

UDB 4.0.0 is a C module for UnrealIRCd 6.2.x. Canonical behavior lives under `src/`; `dist/udb.c` is generated.

## Execution and token budget

- Work synchronously in the foreground. Never launch subagents, background tasks, detached commands, or polling loops.
- Do not use `task`, `invoke_subagent`, `define_subagent`, `manage_task`, `ManageTask`, `/btw`, `/teamwork-preview`, or equivalents.
- For a potentially long command, use one finite foreground timeout when practical. If it expires, inspect the returned output once; do not poll.
- Use this loop: locate -> read minimum evidence -> change/review -> targeted validation -> stop.
- Start from the user-named diff, path, symbol, failure, or test. Search before opening large files and expand only for a concrete dependency or invariant.
- Skills are progressive context. Load only the skill whose trigger matches the task; never load several "just in case".

## Editing discipline

- Inspect `git status --short` once before edits when shell access is available.
- Make the smallest coherent diff. No unrelated refactors, renames, formatting, cleanup, or legacy compatibility unless explicitly requested.
- Preserve unrelated working-tree changes. Never use destructive Git operations or commit/push/merge unless explicitly requested.
- Edit canonical `src/` files, never `dist/udb.c`; before regenerating the bundle, run `scripts/format-sources.sh`, then generate from the formatted canonical sources.
- For documentation, treat `src/` as source of truth and tests as executable evidence. Keep `README.md`/`README_ES.md` and `doc/udb_technical_en.md`/`doc/udb_technical_es.md` semantically aligned.

## UDB correctness invariants

Unless the user explicitly asks for a redesign, preserve all of these:

- fail-closed parsing, validation, authorization, bounds checks, and protocol sequencing;
- staged snapshot data remains isolated from active state until checksum validation and persistence commit succeed;
- peer synchronization requires confirmed `HEL 4` with mandatory `OCL`; authority/import decisions use only eligible directly linked peers;
- bootstrap, propagator selection, failover/failback, and conflict resolution remain deterministic;
- readiness/recovery requires complete convergence of all six persistent blocks `N`, `C`, `I`, `S`, `L`, `K`;
- snapshot transfer remains hop-by-hop; live authorized mutations may relay multihop without bypassing each node's selected authority policy;
- OCL is runtime-only and fail-closed: incomplete/stale participant inventories cannot produce a READY OCLG view;
- malformed, duplicate, replayed, unsolicited, oversized, partial, stale-epoch, or out-of-state input cannot advance DB/OCL state incorrectly;
- runtime effect ownership, cleanup, module unload/reload, callback/event lifetime, `Client *` lifetime, integer arithmetic, NUL termination, and truncation remain safe;
- persistence failures never silently replace valid active state with partial or unvalidated data.

Never weaken an invariant merely to make a test pass.

## Validation

- Start with the smallest relevant check described by `udb-build-test`.
- Broaden only when a shared primitive changed, protocol/convergence crosses subsystems, targeted evidence requires it, or the user explicitly asks for full validation.
- Before runtime/integration tests, rebuild and refresh the installed module as described by `udb-build-test`.
- After source changes that affect distribution, use `udb-bundle-release`.
- After agentic changes, run `.agentic/generate.py --check` and the focused agentic contract tests.
- Final output should be compact: changed/found, validation run, and any concrete residual risk or unverified item.

When the user writes in Spanish, answer in Spanish. Keep source identifiers and repository terminology unchanged.
