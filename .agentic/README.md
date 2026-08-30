# Agentic source of truth

This directory is the single source of truth for CLI-specific agent wrappers and token-safety config.

## Edit

- `AGENTS.md`: small always-on invariants only.
- `.agents/skills/*/SKILL.md`: detailed knowledge loaded progressively.
- `.agentic/roles.yml`: roles, capabilities, skill access, Antigravity tier, and CLI runtime defaults.

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
- `.codex/config.toml` — balanced GPT-5.6 default, low verbosity, multi-agent disabled.

OpenCode model selection intentionally remains session/provider controlled because provider/model IDs differ by installation.

Codex custom subagents are intentionally not generated: UDB specialization lives in progressive skills and the selected primary workflow, avoiding extra model threads/context.

## Verify in the full repository

```bash
./.agentic/ci-check.sh
opencode agent list
agy agents
```

For current model names use `opencode models` and `agy models`; model availability changes faster than repository policy.
