# Agentic source of truth

Do not manually maintain the CLI-specific agent wrappers.

## Prerequisite

Install the generator dependency once:

```bash
python3 -m pip install -r .agentic/requirements.txt
```

## Update roles

Edit:

- `AGENTS.md` for rules that must apply to every agent and CLI.
- `.agents/skills/*/SKILL.md` for reusable UDB knowledge/workflows.
- `.agentic/roles.yml` for agent roles, descriptions, capabilities, skill assignments, and role prompts.

Then regenerate:

```bash
python3 .agentic/generate.py
```

Verify generated wrappers are current:

```bash
python3 .agentic/generate.py --check
```

The check rejects missing, modified, and unexpected wrappers. It also validates role and skill metadata plus both generated frontmatter contracts.

Run the complete local/CI gate with:

```bash
./.agentic/ci-check.sh
```

Generated outputs:

- `.agents/agents/*/agent.md` for Antigravity.
- `.opencode/agents/*.md` for OpenCode.

The generated wrappers carry a `DO NOT EDIT` notice as YAML comments inside frontmatter so both CLIs parse metadata from the first line.

OpenCode model IDs are intentionally not generated from the `model` field because provider/model identifiers are installation-specific. OpenCode therefore keeps the currently selected session model. The `model` field currently controls Antigravity only.

OpenCode permissions use the current singular `permission` mapping. All roles deny `task`; assigned skills are allowlisted; absent core capabilities are denied. The reviewer has no edit or shell capability in either CLI.

After regenerating, restart OpenCode and Antigravity before checking runtime discovery:

```bash
opencode agent list
agy agent
```
