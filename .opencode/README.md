# OpenCode generated agents

`.opencode/agents/*.md` is generated from `.agentic/roles.yml`.

Do not edit these wrappers directly.

OpenCode shares project knowledge from:
- `AGENTS.md`
- `.agents/skills/*/SKILL.md`

Regenerate with:

```bash
python3 .agentic/generate.py
```

Verify with:

```bash
./.agentic/ci-check.sh
```

Restart OpenCode after regeneration because agent files are loaded only at startup.
