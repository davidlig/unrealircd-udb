#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import re
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]
ROLES_FILE = ROOT / ".agentic" / "roles.yml"
AG_ROOT = ROOT / ".agents" / "agents"
OC_ROOT = ROOT / ".opencode" / "agents"

GENERATED_NOTICE = """# AUTO-GENERATED FILE. DO NOT EDIT DIRECTLY.
# Edit .agentic/roles.yml and run:
#     python3 .agentic/generate.py
# To verify generated files are current:
#     python3 .agentic/generate.py --check
"""

AG_CAP_TO_TOOL = {
    "read": "view_file",
    "search": "grep_search",
    "edit": "replace_file_content",
    "shell": "run_command",
}

SUPPORTED_VERSION = 1
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


def load_yaml(text, source):
    try:
        return yaml.load(text, Loader=UniqueKeyLoader)
    except yaml.YAMLError as exc:
        raise SystemExit(f"Invalid YAML in {source}: {exc}") from exc


def parse_frontmatter(content, source):
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


def render_agent(frontmatter, heading, prompt):
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
    tools = [AG_CAP_TO_TOOL[c] for c in capabilities if c in AG_CAP_TO_TOOL]
    skills = [f"skills/{s}" for s in role.get("skills", [])]

    fm = {
        "name": name,
        "description": role["description"],
        "tools": tools,
        "mainAgent": True,
        "subagent": bool(role.get("allow_subagents", defaults.get("allow_subagents", False))),
        "model": role.get("model", defaults.get("model", "inherit")),
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
    if role.get("read_only", False) or "edit" not in role.get("capabilities", []):
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
    fm = {
        "description": role["description"],
        "mode": "primary",
        "permission": opencode_permissions(role),
    }

    # OpenCode model remains session-controlled by default.
    # This intentionally does not translate Antigravity's "pro"/"inherit"
    # because OpenCode provider/model IDs are installation-specific.
    return render_agent(fm, "Role", role["prompt"])

def expected_files(cfg):
    defaults = cfg.get("defaults", {})
    out = {}
    for name, role in cfg["roles"].items():
        out[AG_ROOT / name / "agent.md"] = antigravity_content(name, role, defaults)
        out[OC_ROOT / f"{name}.md"] = opencode_content(name, role, defaults)
    return out

def validate(cfg):
    if set(cfg) - {"version", "defaults", "roles"}:
        unknown = ", ".join(sorted(set(cfg) - {"version", "defaults", "roles"}))
        raise SystemExit(f"Unsupported top-level key(s): {unknown}")
    if type(cfg.get("version")) is not int or cfg["version"] != SUPPORTED_VERSION:
        raise SystemExit(f"Unsupported roles.yml version: {cfg.get('version')!r}")

    defaults = cfg.get("defaults", {})
    if not isinstance(defaults, dict):
        raise SystemExit("defaults must be a mapping")
    unknown_defaults = set(defaults) - {"allow_subagents", "command_execution_policy", "model"}
    if unknown_defaults:
        raise SystemExit(f"Unsupported defaults key(s): {', '.join(sorted(unknown_defaults))}")
    if "allow_subagents" in defaults and not isinstance(defaults["allow_subagents"], bool):
        raise SystemExit("defaults.allow_subagents must be a boolean")
    if defaults.get("allow_subagents", False):
        raise SystemExit("defaults.allow_subagents must remain false")
    if defaults.get("model", "inherit") not in VALID_MODELS:
        raise SystemExit(f"Unsupported default model: {defaults.get('model')!r}")
    if defaults.get("command_execution_policy", "sandbox") not in VALID_COMMAND_POLICIES:
        raise SystemExit(
            f"Unsupported default command_execution_policy: {defaults.get('command_execution_policy')!r}"
        )

    roles = cfg.get("roles")
    if not isinstance(roles, dict) or not roles:
        raise SystemExit("roles must be a non-empty mapping")

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
        if not isinstance(role.get("prompt"), str) or not role["prompt"].strip():
            raise SystemExit(f"{name}: missing prompt")
        capabilities = role.get("capabilities", [])
        if not isinstance(capabilities, list) or not all(isinstance(item, str) for item in capabilities):
            raise SystemExit(f"{name}: capabilities must be a list of strings")
        if len(capabilities) != len(set(capabilities)):
            raise SystemExit(f"{name}: duplicate capability")
        for capability in capabilities:
            if capability not in AG_CAP_TO_TOOL:
                raise SystemExit(f"{name}: unsupported capability {capability!r}")
        skills = role.get("skills", [])
        if not isinstance(skills, list) or not all(isinstance(item, str) for item in skills):
            raise SystemExit(f"{name}: skills must be a list of strings")
        if len(skills) != len(set(skills)):
            raise SystemExit(f"{name}: duplicate skill")
        if "read_only" in role and not isinstance(role["read_only"], bool):
            raise SystemExit(f"{name}: read_only must be a boolean")
        if "allow_subagents" in role and not isinstance(role["allow_subagents"], bool):
            raise SystemExit(f"{name}: allow_subagents must be a boolean")
        if role.get("allow_subagents", defaults.get("allow_subagents", False)):
            raise SystemExit(f"{name}: allow_subagents must remain false")
        if role.get("read_only", False) and ({"edit", "shell"} & set(capabilities)):
            raise SystemExit(f"{name}: read_only roles cannot have edit or shell capabilities")
        if role.get("model", defaults.get("model", "inherit")) not in VALID_MODELS:
            raise SystemExit(f"{name}: unsupported model {role.get('model')!r}")
        policy = role.get(
            "command_execution_policy",
            defaults.get("command_execution_policy", "sandbox"),
        )
        if policy not in VALID_COMMAND_POLICIES:
            raise SystemExit(f"{name}: unsupported command_execution_policy {policy!r}")

        for skill in skills:
            if not NAME_RE.fullmatch(skill):
                raise SystemExit(f"{name}: invalid skill name {skill!r}")
            target = skills_dir / skill / "SKILL.md"
            if not target.exists():
                raise SystemExit(f"{name}: missing skill {skill}: {target}")
            if skill in validated_skills:
                continue
            skill_frontmatter = parse_frontmatter(target.read_text(encoding="utf-8"), target.relative_to(ROOT))
            if skill_frontmatter.get("name") != skill:
                raise SystemExit(f"{target.relative_to(ROOT)}: name must be {skill!r}")
            description = skill_frontmatter.get("description")
            if not isinstance(description, str) or not description.strip():
                raise SystemExit(f"{target.relative_to(ROOT)}: missing description")
            validated_skills.add(skill)


def validate_generated(expected):
    for path, content in expected.items():
        frontmatter = parse_frontmatter(content, path.relative_to(ROOT))
        if path.parent == OC_ROOT:
            if frontmatter.get("mode") != "primary":
                raise SystemExit(f"{path.relative_to(ROOT)}: OpenCode agent must use mode: primary")
            if "permissions" in frontmatter:
                raise SystemExit(f"{path.relative_to(ROOT)}: use permission, not permissions")
            permission = frontmatter.get("permission")
            if not isinstance(permission, dict) or permission.get("task") != "deny":
                raise SystemExit(f"{path.relative_to(ROOT)}: OpenCode task permission must be denied")
        else:
            if frontmatter.get("mainAgent") is not True or frontmatter.get("subagent") is not False:
                raise SystemExit(f"{path.relative_to(ROOT)}: invalid Antigravity main/subagent policy")
            tools = frontmatter.get("tools", [])
            forbidden = {"invoke_subagent", "define_subagent", "manage_task", "ManageTask"}
            if forbidden & set(tools):
                raise SystemExit(f"{path.relative_to(ROOT)}: forbidden delegation tool")


def generated_files(ag_root=AG_ROOT, oc_root=OC_ROOT):
    paths = set()
    if ag_root.exists():
        paths.update(ag_root.glob("*/agent.md"))
    if oc_root.exists():
        paths.update(oc_root.glob("*.md"))
    return paths


def unexpected_files(expected, ag_root=AG_ROOT, oc_root=OC_ROOT):
    return generated_files(ag_root, oc_root) - set(expected)

def clean_stale(expected):
    for path in sorted(unexpected_files(expected)):
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
                current.splitlines(),
                content.splitlines(),
                fromfile=str(path.relative_to(ROOT)),
                tofile="expected",
                lineterm="",
            )
            for line in list(diff)[:40]:
                print(line)
            ok = False
    for path in sorted(unexpected_files(expected)):
        print(f"UNEXPECTED: {path.relative_to(ROOT)}")
        ok = False
    if not ok:
        return 1
    print("Generated agent wrappers are up to date.")
    return 0

def generate(expected):
    for path, content in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        print(f"generated {path.relative_to(ROOT)}")
    clean_stale(expected)
    return 0

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify wrappers match roles.yml")
    args = parser.parse_args()

    cfg = load_config()
    validate(cfg)
    expected = expected_files(cfg)
    validate_generated(expected)

    if args.check:
        return check(expected)
    return generate(expected)

if __name__ == "__main__":
    raise SystemExit(main())
