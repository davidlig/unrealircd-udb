#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
import re
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
ROLES_FILE = ROOT / ".agentic" / "roles.yml"
AG_ROOT = ROOT / ".agents" / "agents"
OC_ROOT = ROOT / ".opencode" / "agents"
OPENCODE_CONFIG = ROOT / "opencode.json"
CODEX_CONFIG = ROOT / ".codex" / "config.toml"

GENERATED_NOTICE = """# AUTO-GENERATED FILE. DO NOT EDIT DIRECTLY.
# Edit .agentic/roles.yml and run:
#     python3 .agentic/generate.py
# Verify with:
#     python3 .agentic/generate.py --check
"""

AG_CAP_TO_TOOL = {
    "read": "view_file",
    "search": "grep_search",
    "edit": "replace_file_content",
    "shell": "run_command",
}

SUPPORTED_VERSION = 2
NAME_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$")
VALID_MODELS = {"inherit", "flash", "pro"}
VALID_COMMAND_POLICIES = {"off", "auto", "eager", "sandbox"}
ROLE_KEYS = {
    "description",
    "model",
    "capabilities",
    "skills",
    "read_only",
    "allow_subagents",
    "command_execution_policy",
    "prompt",
}


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def construct_unique_mapping(loader, node, deep=False):
    loader.flatten_mapping(node)
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise yaml.constructor.ConstructorError(
                "while constructing a mapping",
                node.start_mark,
                f"found duplicate key {key!r}",
                key_node.start_mark,
            )
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
    construct_unique_mapping,
)


def load_yaml(text: str, source):
    try:
        return yaml.load(text, Loader=UniqueKeyLoader)
    except yaml.YAMLError as exc:
        raise SystemExit(f"Invalid YAML in {source}: {exc}") from exc


def parse_frontmatter(content: str, source):
    lines = content.splitlines()
    if not lines or lines[0] != "---":
        raise SystemExit(f"{source}: YAML frontmatter must start on line 1")
    try:
        end = lines.index("---", 1)
    except ValueError as exc:
        raise SystemExit(f"{source}: missing closing YAML frontmatter delimiter") from exc
    data = load_yaml("\n".join(lines[1:end]), source)
    if not isinstance(data, dict):
        raise SystemExit(f"{source}: YAML frontmatter must be a mapping")
    return data


def load_config():
    try:
        cfg = load_yaml(ROLES_FILE.read_text(encoding="utf-8"), ROLES_FILE.relative_to(ROOT))
    except OSError as exc:
        raise SystemExit(f"Cannot read {ROLES_FILE}: {exc}") from exc
    if not isinstance(cfg, dict) or "roles" not in cfg:
        raise SystemExit("Invalid .agentic/roles.yml")
    return cfg


def render_agent(frontmatter, heading: str, prompt: str) -> str:
    return (
        "---\n"
        + GENERATED_NOTICE
        + yaml.safe_dump(frontmatter, sort_keys=False, allow_unicode=True).rstrip()
        + f"\n---\n\n# {heading}\n\n"
        + prompt.rstrip()
        + "\n"
    )


def antigravity_content(name, role, defaults):
    capabilities = role.get("capabilities", [])
    tools = [AG_CAP_TO_TOOL[c] for c in capabilities]
    skills = [f"skills/{s}" for s in role.get("skills", [])]
    fm = {
        "name": name,
        "description": role["description"],
        "tools": tools,
        "mainAgent": True,
        "subagent": False,
        "model": role.get("model", defaults.get("model", "flash")),
        "commandExecutionPolicy": role.get(
            "command_execution_policy",
            defaults.get("command_execution_policy", "sandbox"),
        ),
    }
    if skills:
        fm["skills"] = skills
    return render_agent(fm, "System Prompt", role["prompt"])


def opencode_permissions(role):
    capabilities = set(role.get("capabilities", []))
    permissions = {
        "task": "deny",
        "skill": {"*": "deny"},
    }
    for skill in role.get("skills", []):
        permissions["skill"][skill] = "allow"

    if "read" not in capabilities:
        permissions["read"] = "deny"
        permissions["list"] = "deny"
    if "search" not in capabilities:
        permissions["glob"] = "deny"
        permissions["grep"] = "deny"
        permissions["lsp"] = "deny"
    if role.get("read_only", False) or "edit" not in capabilities:
        permissions["edit"] = "deny"
    if role.get("read_only", False) or "shell" not in capabilities:
        permissions["bash"] = "deny"
    else:
        permissions["bash"] = {
            "git push": "deny",
            "git push *": "deny",
            "git reset --hard": "deny",
            "git reset --hard *": "deny",
            "git clean": "deny",
            "git clean *": "deny",
        }
    return permissions


def opencode_content(name, role, defaults):
    # Model is deliberately session-controlled: the user's OpenAI/GLM provider IDs
    # are installation-specific. Project config only enforces orchestration safety.
    fm = {
        "description": role["description"],
        "mode": "primary",
        "permission": opencode_permissions(role),
    }
    return render_agent(fm, "Role", role["prompt"])


def render_opencode_config(runtime):
    cfg = {
        "$schema": "https://opencode.ai/config.json",
        "default_agent": runtime["default_agent"],
        "subagent_depth": runtime["subagent_depth"],
    }
    return json.dumps(cfg, indent=2, ensure_ascii=False) + "\n"


def render_codex_config(runtime):
    multi_agent = "true" if runtime["multi_agent"] else "false"
    return (
        GENERATED_NOTICE
        + f'model = "{runtime["model"]}"\n'
        + f'model_reasoning_effort = "{runtime["reasoning_effort"]}"\n'
        + f'model_verbosity = "{runtime["verbosity"]}"\n\n'
        + "[features]\n"
        + f"multi_agent = {multi_agent}\n"
    )


def expected_files(cfg):
    defaults = cfg.get("defaults", {})
    runtime = cfg["runtime"]
    out = {}
    for name, role in cfg["roles"].items():
        out[AG_ROOT / name / "agent.md"] = antigravity_content(name, role, defaults)
        out[OC_ROOT / f"{name}.md"] = opencode_content(name, role, defaults)
    out[OPENCODE_CONFIG] = render_opencode_config(runtime["opencode"])
    out[CODEX_CONFIG] = render_codex_config(runtime["codex"])
    return out


def _require_mapping(value, label):
    if not isinstance(value, dict):
        raise SystemExit(f"{label} must be a mapping")
    return value


def validate(cfg):
    allowed_top = {"version", "defaults", "runtime", "roles"}
    unknown = set(cfg) - allowed_top
    if unknown:
        raise SystemExit(f"Unsupported top-level key(s): {', '.join(sorted(unknown))}")
    if type(cfg.get("version")) is not int or cfg["version"] != SUPPORTED_VERSION:
        raise SystemExit(f"Unsupported roles.yml version: {cfg.get('version')!r}")

    defaults = _require_mapping(cfg.get("defaults", {}), "defaults")
    allowed_defaults = {"allow_subagents", "command_execution_policy", "model"}
    unknown_defaults = set(defaults) - allowed_defaults
    if unknown_defaults:
        raise SystemExit(f"Unsupported defaults key(s): {', '.join(sorted(unknown_defaults))}")
    if defaults.get("allow_subagents", False) is not False:
        raise SystemExit("defaults.allow_subagents must remain false")
    if defaults.get("model", "flash") not in VALID_MODELS:
        raise SystemExit(f"Unsupported default model: {defaults.get('model')!r}")
    if defaults.get("command_execution_policy", "sandbox") not in VALID_COMMAND_POLICIES:
        raise SystemExit("Unsupported default command_execution_policy")

    runtime = _require_mapping(cfg.get("runtime"), "runtime")
    if set(runtime) != {"opencode", "codex"}:
        raise SystemExit("runtime must contain exactly opencode and codex")
    oc_runtime = _require_mapping(runtime["opencode"], "runtime.opencode")
    if set(oc_runtime) != {"default_agent", "subagent_depth"}:
        raise SystemExit("runtime.opencode must contain default_agent and subagent_depth")
    if oc_runtime["subagent_depth"] != 0:
        raise SystemExit("runtime.opencode.subagent_depth must remain 0")

    codex = _require_mapping(runtime["codex"], "runtime.codex")
    if set(codex) != {"model", "reasoning_effort", "verbosity", "multi_agent"}:
        raise SystemExit("runtime.codex has unsupported/missing keys")
    if not isinstance(codex["model"], str) or not codex["model"].strip():
        raise SystemExit("runtime.codex.model must be a string")
    if codex["reasoning_effort"] not in {"minimal", "low", "medium", "high", "xhigh"}:
        raise SystemExit("runtime.codex.reasoning_effort is invalid")
    if codex["verbosity"] not in {"low", "medium", "high"}:
        raise SystemExit("runtime.codex.verbosity is invalid")
    if codex["multi_agent"] is not False:
        raise SystemExit("runtime.codex.multi_agent must remain false")

    roles = _require_mapping(cfg.get("roles"), "roles")
    if not roles:
        raise SystemExit("roles must be non-empty")
    if oc_runtime["default_agent"] not in roles:
        raise SystemExit("runtime.opencode.default_agent must name a configured role")

    skills_dir = ROOT / ".agents" / "skills"
    validated_skills = set()
    for name, role in roles.items():
        if not isinstance(name, str) or not NAME_RE.fullmatch(name):
            raise SystemExit(f"Invalid role name: {name!r}")
        if not isinstance(role, dict):
            raise SystemExit(f"{name}: role must be a mapping")
        unknown_role_keys = set(role) - ROLE_KEYS
        if unknown_role_keys:
            raise SystemExit(f"{name}: unsupported key(s): {', '.join(sorted(unknown_role_keys))}")
        if not isinstance(role.get("description"), str) or not role["description"].strip():
            raise SystemExit(f"{name}: missing description")
        if len(role["description"]) > 180:
            raise SystemExit(f"{name}: description exceeds 180 chars (token budget)")
        if not isinstance(role.get("prompt"), str) or not role["prompt"].strip():
            raise SystemExit(f"{name}: missing prompt")
        if len(role["prompt"]) > 700:
            raise SystemExit(f"{name}: prompt exceeds 700 chars (token budget)")

        capabilities = role.get("capabilities", [])
        if not isinstance(capabilities, list) or len(capabilities) != len(set(capabilities)):
            raise SystemExit(f"{name}: capabilities must be a unique list")
        for capability in capabilities:
            if capability not in AG_CAP_TO_TOOL:
                raise SystemExit(f"{name}: unsupported capability {capability!r}")

        skills = role.get("skills", [])
        if not isinstance(skills, list) or len(skills) != len(set(skills)):
            raise SystemExit(f"{name}: skills must be a unique list")
        if role.get("read_only", False) and ({"edit", "shell"} & set(capabilities)):
            raise SystemExit(f"{name}: read_only roles cannot edit or shell")
        if role.get("allow_subagents", defaults.get("allow_subagents", False)):
            raise SystemExit(f"{name}: allow_subagents must remain false")
        if role.get("model", defaults.get("model", "flash")) not in VALID_MODELS:
            raise SystemExit(f"{name}: unsupported model")
        policy = role.get("command_execution_policy", defaults.get("command_execution_policy", "sandbox"))
        if policy not in VALID_COMMAND_POLICIES:
            raise SystemExit(f"{name}: unsupported command_execution_policy")

        for skill in skills:
            if not isinstance(skill, str) or not NAME_RE.fullmatch(skill):
                raise SystemExit(f"{name}: invalid skill {skill!r}")
            target = skills_dir / skill / "SKILL.md"
            if not target.exists():
                raise SystemExit(f"{name}: missing skill {skill}: {target}")
            if skill in validated_skills:
                continue
            fm = parse_frontmatter(target.read_text(encoding="utf-8"), target.relative_to(ROOT))
            if fm.get("name") != skill:
                raise SystemExit(f"{target.relative_to(ROOT)}: name must be {skill!r}")
            description = fm.get("description")
            if not isinstance(description, str) or not description.strip():
                raise SystemExit(f"{target.relative_to(ROOT)}: missing description")
            if len(description) > 200:
                raise SystemExit(f"{target.relative_to(ROOT)}: description exceeds 200 chars")
            validated_skills.add(skill)


def validate_generated(expected):
    forbidden_ag = {"invoke_subagent", "define_subagent", "manage_task", "ManageTask"}
    for path, content in expected.items():
        if path.parent == OC_ROOT:
            fm = parse_frontmatter(content, path.relative_to(ROOT))
            if fm.get("mode") != "primary":
                raise SystemExit(f"{path.relative_to(ROOT)}: OpenCode agent must be primary")
            permission = fm.get("permission")
            if not isinstance(permission, dict) or permission.get("task") != "deny":
                raise SystemExit(f"{path.relative_to(ROOT)}: task permission must be denied")
        elif path.name == "agent.md" and AG_ROOT in path.parents:
            fm = parse_frontmatter(content, path.relative_to(ROOT))
            if fm.get("mainAgent") is not True or fm.get("subagent") is not False:
                raise SystemExit(f"{path.relative_to(ROOT)}: invalid Antigravity main/subagent policy")
            if forbidden_ag & set(fm.get("tools", [])):
                raise SystemExit(f"{path.relative_to(ROOT)}: forbidden delegation tool")


def generated_agent_files(ag_root=AG_ROOT, oc_root=OC_ROOT):
    paths = set()
    if ag_root.exists():
        paths.update(ag_root.glob("*/agent.md"))
    if oc_root.exists():
        paths.update(oc_root.glob("*.md"))
    return paths


def unexpected_agent_files(expected, ag_root=AG_ROOT, oc_root=OC_ROOT):
    expected_agents = {p for p in expected if (p.parent == oc_root or ag_root in p.parents)}
    return generated_agent_files(ag_root, oc_root) - expected_agents


def clean_stale(expected):
    for path in sorted(unexpected_agent_files(expected)):
        path.unlink()
        if path.parent not in {AG_ROOT, OC_ROOT}:
            try:
                path.parent.rmdir()
            except OSError:
                pass


def check(expected):
    ok = True
    for path, content in expected.items():
        if not path.exists():
            print(f"MISSING: {path.relative_to(ROOT)}")
            ok = False
            continue
        current = path.read_text(encoding="utf-8")
        if current != content:
            print(f"OUTDATED: {path.relative_to(ROOT)}")
            diff = difflib.unified_diff(
                current.splitlines(), content.splitlines(),
                fromfile=str(path.relative_to(ROOT)), tofile="expected", lineterm="",
            )
            for line in list(diff)[:40]:
                print(line)
            ok = False
    for path in sorted(unexpected_agent_files(expected)):
        print(f"UNEXPECTED: {path.relative_to(ROOT)}")
        ok = False
    if ok:
        print("Generated agentic files are up to date.")
        return 0
    return 1


def generate(expected):
    for path, content in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        print(f"generated {path.relative_to(ROOT)}")
    clean_stale(expected)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify generated agentic files")
    args = parser.parse_args()

    cfg = load_config()
    validate(cfg)
    expected = expected_files(cfg)
    validate_generated(expected)
    return check(expected) if args.check else generate(expected)


if __name__ == "__main__":
    raise SystemExit(main())
