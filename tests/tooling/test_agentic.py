"""Semantic generator contracts plus malformed configuration rejection."""

import copy
import json
import tomllib

import pytest

from udb_test_support.modules import ROOT, repository_module

pytestmark = pytest.mark.tooling
ROLES = ["udb-developer", "udb-reviewer", "udb-sync-specialist", "udb-test-engineer", "udb-documenter"]


@pytest.fixture
def generator():
    return repository_module(".agentic/generate.py")


@pytest.mark.parametrize("role", ROLES)
def test_generated_role_contract_is_semantic_and_in_sync(generator, role):
    config = generator.load_config()
    generator.validate(config)
    outputs = generator.expected_files(config)
    generator.validate_generated(outputs)
    ag_path = generator.AG_ROOT / role / "agent.md"
    oc_path = generator.OC_ROOT / f"{role}.md"
    ag = generator.parse_frontmatter(outputs[ag_path], ag_path)
    oc = generator.parse_frontmatter(outputs[oc_path], oc_path)
    assert ag["mainAgent"] is True and ag["subagent"] is False
    assert ag["model"] == "flash"
    assert not {"manage_task", "invoke_subagent", "define_subagent", "ManageTask"} & set(ag["tools"])
    assert oc["mode"] == "primary" and oc["permission"]["task"] == "deny"
    assert oc["permission"]["skill"]["*"] == "deny"
    assert outputs[ag_path] == ag_path.read_text()
    assert outputs[oc_path] == oc_path.read_text()
    if role == "udb-reviewer":
        assert oc["permission"]["edit"] == oc["permission"]["bash"] == "deny"
        assert not {"replace_file_content", "run_command"} & set(ag["tools"])
    if role == "udb-documenter":
        assert "udb-documentation" in config["roles"][role]["skills"]
        assert "udb-bundle-release" not in config["roles"][role]["skills"]
    else:
        assert "udb-operclasses" in config["roles"][role]["skills"]
    assert len(config["roles"][role]["prompt"]) <= 700
    assert len(config["roles"][role]["description"]) <= 180


def test_runtime_outputs_follow_config_without_inventing_model_overrides(generator):
    outputs = generator.expected_files(generator.load_config())
    oc = json.loads(outputs[generator.OPENCODE_CONFIG])
    cx = tomllib.loads(outputs[generator.CODEX_CONFIG])
    assert oc["default_agent"] == "udb-developer" and oc["subagent_depth"] == 0
    assert "model" not in oc and "model" not in cx
    assert cx["model_reasoning_effort"] == "medium" and cx["model_verbosity"] == "low"
    assert cx["features"]["multi_agent"] is False
    assert (ROOT / "AGENTS.md").stat().st_size <= 4096


@pytest.mark.parametrize("text", ["", "name: no-frontmatter", "---\nname: missing-end",
    "---\n- list\n---", "---\nname: one\nname: two\n---", "---\nname: [\n---"])
def test_invalid_frontmatter_is_rejected(generator, text):
    with pytest.raises(SystemExit):
        generator.parse_frontmatter(text, "test.md")


@pytest.mark.parametrize("path,value", [
    ("version", 99), ("version", True), ("defaults.allow_subagents", True),
    ("defaults.model", "unknown"), ("defaults.command_execution_policy", "unsafe"),
    ("runtime.opencode.subagent_depth", 1), ("runtime.opencode.default_agent", "absent"),
    ("runtime.codex.multi_agent", True), ("runtime.codex.reasoning_effort", "unknown"),
    ("runtime.codex.verbosity", "unknown"),
    ("roles.udb-reviewer.capabilities", ["read", "edit"]),
    ("roles.udb-developer.capabilities", ["read", "read"]),
    ("roles.udb-developer.capabilities", ["invalid"]),
    ("roles.udb-developer.skills", ["udb-core", "udb-core"]),
    ("roles.udb-developer.skills", ["nonexistent"]),
    ("roles.udb-developer.prompt", "x" * 701),
    ("roles.udb-developer.description", "x" * 181),
    ("roles.udb-developer.allow_subagents", True),
])
def test_invalid_configuration_never_generates_permissive_agents(generator, path, value):
    config = copy.deepcopy(generator.load_config())
    target = config
    keys = path.split(".")
    for key in keys[:-1]:
        target = target[key]
    target[keys[-1]] = value
    with pytest.raises(SystemExit):
        generator.validate(config)


def test_unexpected_agents_are_detected_in_both_runtime_trees(generator, tmp_path):
    ag, oc = tmp_path / "ag", tmp_path / "oc"
    expected = ag / "current/agent.md"
    extras = {ag / "obsolete/agent.md", oc / "obsolete.md"}
    for path in extras | {expected}:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("test")
    assert generator.unexpected_agent_files({expected: "test"}, ag, oc) == extras


def test_release_skill_prescribes_format_before_bundle():
    text = (ROOT / ".agents/skills/udb-bundle-release/SKILL.md").read_text()
    assert text.index("scripts/format-sources.sh") < text.index("python3 scripts/bundle.py")


@pytest.mark.parametrize("skill,requirements", [
    ("udb-security", ["root `(source SID, epoch)`", "TLS certificate verification"]),
    ("udb-sync-protocol", ["validated `INF`, `BEGIN`, and `END` frames resolve to one exact watermark", "only the root authority"]),
])
def test_agent_domain_guidance_preserves_stream_security(skill, requirements):
    text = (ROOT / f".agents/skills/{skill}/SKILL.md").read_text()
    assert all(requirement in text for requirement in requirements)
