"""Semantic generator contracts plus malformed configuration rejection."""

import copy
import json
import os
import re
import subprocess
import sys
import tomllib
import unittest

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


@pytest.mark.parametrize("skill", ["udb-build-test", "udb-operclasses"])
def test_skill_test_routes_exist_in_the_current_suite(skill):
    text = (ROOT / f".agents/skills/{skill}/SKILL.md").read_text()
    routes = re.findall(r"`([^`\s]+\.(?:py|c))`", text)
    assert len(routes) >= 3
    for route in routes:
        assert route.startswith("tests/"), f"Use a repository-relative test path: {route}"
        assert (ROOT / route).is_file(), f"Retired or missing test: {route}"


def test_runtime_guidance_uses_checkout_module_without_installing_it():
    policy = (ROOT / "AGENTS.md").read_text()
    skill = (ROOT / ".agents/skills/udb-build-test/SKILL.md").read_text()
    assert "UDB_MODULE_PATH" in policy and "UDB_MODULE_PATH" in skill
    assert "refresh the installed module" not in policy
    assert not re.search(r"(?m)^\s*cp\s+.*udb\.so", skill)
    assert "tests/README.md" in skill


@pytest.mark.parametrize("fail", [False, True])
def test_agentic_checker_runs_only_current_tooling_and_generator(tmp_path, fail):
    executable = tmp_path / "python3"
    log = tmp_path / "commands.jsonl"
    executable.write_text(f"#!{sys.executable}\n"
        "import json, sys\n"
        f"with open({str(log)!r}, 'a') as stream: stream.write(json.dumps(sys.argv[1:]) + '\\n')\n"
        f"sys.exit(23 if {fail!r} and sys.argv[1:3] == ['-m', 'pytest'] else 0)\n")
    executable.chmod(0o755)
    result = subprocess.run(["sh", str(ROOT / ".agentic/ci-check.sh")], cwd=tmp_path,
        env={**os.environ, "PATH": f"{tmp_path}:{os.environ.get('PATH', '')}"},
        capture_output=True, text=True, timeout=10)
    commands = [json.loads(line) for line in log.read_text().splitlines()]
    expected = [["-m", "pytest", "-q", "-m", "tooling",
                 "tests/tooling/test_agentic.py", "tests/tooling/test_ci.py"]]
    if not fail:
        expected.append([".agentic/generate.py", "--check"])
    assert result.returncode == (23 if fail else 0), result.stderr
    assert commands == expected


@pytest.fixture(params=[".so", ".dll"])
def freshness_tree(tmp_path, monkeypatch, request):
    checker = repository_module(".agentic/test_runtime_module_fresh.py")
    source = tmp_path / "src/udb.c"
    source.parent.mkdir()
    source.write_text("source")
    os.utime(source, (1, 1))
    bundle = tmp_path / "dist/udb.c"
    bundle.parent.mkdir()
    bundle.write_text("generated source")
    os.utime(bundle, (1, 1))
    candidates = (tmp_path / f"src/udb{request.param}", tmp_path / f"dist/udb{request.param}")
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    monkeypatch.setattr(checker, "MODULE_EXTENSION", request.param)
    monkeypatch.setattr(checker, "MODULE_CANDIDATES", candidates)
    monkeypatch.delenv("UDB_MODULE_PATH", raising=False)
    runtime = tmp_path / "user-runtime"
    monkeypatch.setenv("UDB_TEST_IRCD_ROOT", str(runtime))
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
    installed = runtime / "modules/third/udb.so"
    installed.parent.mkdir(parents=True)
    installed.write_bytes(b"unrelated installed module")
    return checker, source, candidates, installed


def run_freshness(checker):
    result = unittest.TestResult()
    unittest.defaultTestLoader.loadTestsFromModule(checker).run(result)
    assert result.testsRun == 1
    assert not result.skipped
    return result


@pytest.mark.parametrize("location", ["src", "dist", "override", "missing_override"])
@pytest.mark.parametrize("installed_present", [False, True])
def test_freshness_selects_module_like_isolated_runtime(freshness_tree, monkeypatch, location, installed_present):
    checker, source, candidates, installed = freshness_tree
    built = candidates[1 if location == "dist" else 0]
    built.parent.mkdir(exist_ok=True)
    built.write_bytes(b"fresh build")
    os.utime(built, (2, 2))
    if location == "src":
        candidates[1].parent.mkdir(exist_ok=True)
        candidates[1].write_bytes(b"different distribution build")
    if location in {"override", "missing_override"}:
        override = source.parent.parent / "selected.so"
        if location == "override":
            override.write_bytes(b"fresh build")
        monkeypatch.setenv("UDB_MODULE_PATH", str(override))
    if not installed_present:
        installed.unlink()
    before = installed.read_bytes() if installed_present else None
    result = run_freshness(checker)
    assert result.wasSuccessful(), result.failures + result.errors
    assert (installed.read_bytes() if installed.is_file() else None) == before


@pytest.mark.parametrize("fault", ["missing", "stale_source", "wrong_selected_digest"])
def test_freshness_rejects_missing_or_stale_build_without_skips(freshness_tree, monkeypatch, fault):
    checker, source, candidates, installed = freshness_tree
    if fault != "missing":
        candidates[0].write_bytes(b"fresh build")
        os.utime(candidates[0], (2, 2))
    if fault == "stale_source":
        os.utime(source, (3, 3))
    if fault == "wrong_selected_digest":
        override = source.parent.parent / "selected.so"
        override.write_bytes(b"old build")
        monkeypatch.setenv("UDB_MODULE_PATH", str(override))
    before = installed.read_bytes()
    result = run_freshness(checker)
    assert result.failures and not result.errors
    assert installed.read_bytes() == before


@pytest.mark.parametrize("location", ["src", "dist", "copy_src", "copy_dist"])
def test_freshness_checks_selected_artifact_when_build_hashes_differ(freshness_tree, monkeypatch, location):
    checker, source, candidates, installed = freshness_tree
    for index, built in enumerate(candidates):
        built.write_bytes(f"build {index}".encode())
        os.utime(built, (2, 2))
    selected = candidates[1 if location.endswith("dist") else 0]
    if location.startswith("copy_"):
        copy = source.parent.parent / f"selected{checker.MODULE_EXTENSION}"
        copy.write_bytes(selected.read_bytes())
        selected = copy
    monkeypatch.setenv("UDB_MODULE_PATH", str(selected))
    result = run_freshness(checker)
    assert result.wasSuccessful(), result.failures + result.errors
    assert installed.read_bytes() == b"unrelated installed module"


@pytest.mark.parametrize("index", [0, 1])
def test_freshness_direct_stale_selection_cannot_use_other_fresh_build(freshness_tree, monkeypatch, index):
    checker, source, candidates, installed = freshness_tree
    os.utime(source, (3, 3))
    for built in candidates:
        built.write_bytes(b"identical builds")
        os.utime(built, (4, 4))
    os.utime(candidates[index], (2, 2))
    monkeypatch.setenv("UDB_MODULE_PATH", str(candidates[index]))
    result = run_freshness(checker)
    assert result.failures and not result.errors
    assert "Stale build" in result.failures[0][1]


@pytest.mark.parametrize("fresh_copy", [False, True])
def test_freshness_external_identical_copy_requires_a_fresh_matching_build(freshness_tree, monkeypatch, fresh_copy):
    checker, source, candidates, installed = freshness_tree
    os.utime(source, (3, 3))
    for built in candidates:
        built.write_bytes(b"identical builds")
        os.utime(built, (2, 2))
    if fresh_copy:
        os.utime(candidates[1], (4, 4))
    copy = source.parent.parent / f"selected{checker.MODULE_EXTENSION}"
    copy.write_bytes(b"identical builds")
    monkeypatch.setenv("UDB_MODULE_PATH", str(copy))
    result = run_freshness(checker)
    assert result.wasSuccessful() == fresh_copy, result.failures + result.errors
    assert not result.errors


@pytest.mark.parametrize("index", [0, 1])
@pytest.mark.parametrize("suffix", [".c", ".h", ".inc"])
def test_freshness_checks_all_canonical_inputs_for_each_artifact(freshness_tree, monkeypatch, index, suffix):
    checker, source, candidates, installed = freshness_tree
    built = candidates[index]
    built.write_bytes(b"build")
    os.utime(built, (2, 2))
    dependency = source.parent / "nested" / f"input{suffix}"
    dependency.parent.mkdir()
    dependency.write_text("updated input")
    os.utime(dependency, (3, 3))
    monkeypatch.setenv("UDB_MODULE_PATH", str(built))
    result = run_freshness(checker)
    assert result.failures and not result.errors
    assert "Stale build" in result.failures[0][1]


@pytest.mark.parametrize("index", [0, 1])
@pytest.mark.parametrize("bundle_fault", ["newer", "missing"])
def test_freshness_requires_generated_input_only_for_dist(freshness_tree, monkeypatch, index, bundle_fault):
    checker, source, candidates, installed = freshness_tree
    for built in candidates:
        built.write_bytes(b"identical builds")
        os.utime(built, (2, 2))
    bundle = source.parent.parent / "dist/udb.c"
    if bundle_fault == "newer":
        os.utime(bundle, (3, 3))
    else:
        bundle.unlink()
    monkeypatch.setenv("UDB_MODULE_PATH", str(candidates[index]))
    result = run_freshness(checker)
    assert result.wasSuccessful() == (index == 0), result.failures + result.errors
    assert not result.errors


def test_freshness_rejects_stale_external_dist_even_with_fresh_src(freshness_tree, monkeypatch):
    checker, source, candidates, installed = freshness_tree
    os.utime(source, (3, 3))
    for index, built in enumerate(candidates):
        built.write_bytes(f"build {index}".encode())
        timestamp = 4 if index == 0 else 2
        os.utime(built, (timestamp, timestamp))
    copy = source.parent.parent / f"selected{checker.MODULE_EXTENSION}"
    copy.write_bytes(candidates[1].read_bytes())
    monkeypatch.setenv("UDB_MODULE_PATH", str(copy))
    result = run_freshness(checker)
    assert result.failures and not result.errors
    assert "Stale build" in result.failures[0][1]
