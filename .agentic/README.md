# Agentic source of truth

This directory is the single source of truth for CLI-specific agent wrappers and token-safety config.

## Edit

- `AGENTS.md`: small always-on project invariants only.
- `.agents/skills/*/SKILL.md`: detailed knowledge loaded progressively.
- `.agentic/roles.yml`: roles, capabilities, skill access, Antigravity tier, and CLI runtime defaults.
- `.agentic/generate.py`: renderer/validator for generated CLI surfaces.

Do not manually maintain generated agent wrappers or runtime config.

## Generate

```bash
python3 -m pip install -r .agentic/requirements.txt
python3 .agentic/generate.py
python3 .agentic/generate.py --check
```

Generated surfaces:
- `.agents/agents/*/agent.md` — Antigravity primary agents; `subagent: false` and no delegation tools.
- `.opencode/agents/*.md` — OpenCode primary agents; `permission.task: deny`.
- `opencode.json` — default UDB agent plus `subagent_depth: 0`.
- `.codex/config.toml` — reasoning/verbosity policy with multi-agent disabled; model selection stays session-controlled.

OpenCode and Codex model selection intentionally remain session/provider controlled. Model IDs and availability change faster than repository policy; use the CLI's current model listing when an explicit model is needed.

Antigravity keeps the low-cost `flash` tier as the generated default. Promote a role explicitly only when a task requires it; do not use extra agents to compensate for model choice.

Codex custom subagents are intentionally not generated. UDB specialization lives in progressive skills and the selected primary workflow, avoiding extra model threads/context.

## Current specialization

- `udb-sync-protocol` owns HEL 4, DB reconciliation, propagator/bootstrap/readiness and mutation convergence.
- `udb-operclasses` owns runtime OCL inventories, epochs, membership, replay and OCLG completeness/intersection.
- `udb-documentation` owns evidence-driven synchronization of the English/Spanish README and technical docs.
- `udb-security` owns strict wire parsing, root stream/watermark checks, bounded-state fail-closed behavior, secret redaction, and the server-link TLS trust boundary.

## Verify in the full repository

```bash
./.agentic/ci-check.sh
opencode agent list
agy agents
```

For current model names use each CLI/provider's own model-listing command rather than committing transient model IDs to the repository.
