# Antigravity + shared skills

- `.agents/skills/` is hand-maintained shared knowledge used by Antigravity and OpenCode.
- `.agents/agents/` is generated from `.agentic/roles.yml`.

Do not edit generated agent wrappers directly.

Regenerate with:

```bash
python3 .agentic/generate.py
```

Verify with:

```bash
./.agentic/ci-check.sh
```
