"""The pytest suites are the sole authoritative UDB test entrypoints in CI."""

import re
import yaml
import pytest

from udb_test_support.modules import ROOT

pytestmark = pytest.mark.tooling


@pytest.fixture
def workflow():
    return yaml.safe_load((ROOT / ".github/workflows/ci.yml").read_text())


def test_current_suite_is_required_in_both_existing_build_profiles(workflow):
    job = workflow["jobs"]["test"]
    profiles = {item["profile"]: item for item in job["strategy"]["matrix"]["include"]}
    assert set(profiles) == {"normal", "asan"}
    assert profiles["normal"]["sanitizer"] == ""
    assert profiles["asan"]["sanitizer"] == "asan"
    scripts = [step for step in job["steps"] if "python3 -m pytest" in step.get("run", "")]
    assert len(scripts) == 2
    assert all(not step.get("continue-on-error", False) and "if" not in step for step in scripts)
    unit = next(step for step in scripts if "unit or tooling" in step["run"])
    runtime = next(step for step in scripts if "integration or protocol or recovery or model" in step["run"])
    assert '--c-profile "${{ matrix.profile }}"' in unit["run"]
    assert unit["env"]["ASAN_OPTIONS"] == "detect_leaks=1:abort_on_error=1:detect_odr_violation=2"
    assert all("--junitxml" in step["run"] and "$RUNNER_TEMP/" in step["run"] for step in scripts)
    assert runtime["run"].count("python3 -m pytest") == 1


def test_dependency_bootstrap_is_portable_and_precedes_new_suite(workflow):
    steps = workflow["jobs"]["test"]["steps"]
    bootstrap = next(i for i, step in enumerate(steps) if "tests/support/bootstrap_cmocka.py" in step.get("run", ""))
    first_test = next(i for i, step in enumerate(steps) if "python3 -m pytest" in step.get("run", ""))
    assert bootstrap < first_test
    script = steps[bootstrap]["run"]
    assert 'python3 -m venv "$RUNNER_TEMP/udb-test-venv"' in script
    assert "-r tests/requirements.txt" in script
    assert "$GITHUB_PATH" in script
    assert not steps[bootstrap].get("continue-on-error", False)


def test_no_standalone_legacy_test_scripts_are_ci_entrypoints(workflow):
    test_steps = workflow["jobs"]["test"]["steps"]
    script = "\n".join(step.get("run", "") for step in test_steps)
    assert script.count("python3 -m pytest") == 2
    assert not re.search(r"(?m)^\s*python3 tests/(?!support/bootstrap_cmocka\.py\b)[^\s]+\.py\b", script)
    release_script = "\n".join(step.get("run", "") for step in workflow["jobs"]["release"]["steps"])
    assert "tests/test_release_manifest.py" not in release_script
    assert (ROOT / "tests/tooling/test_release.py").is_file()
