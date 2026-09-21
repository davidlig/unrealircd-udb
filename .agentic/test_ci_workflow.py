from __future__ import annotations

import re
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"


class WorkflowContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding="utf-8")
        cls.workflow = yaml.safe_load(cls.text)
        cls.jobs = cls.workflow["jobs"]
        cls.test_job = cls.jobs["test"]
        cls.run_scripts = "\n".join(step.get("run", "") for step in cls.test_job["steps"])

    def test_normal_and_sanitized_builds_share_one_matrix_job(self):
        self.assertNotIn("sanitizers", self.jobs)
        profiles = {entry["profile"]: entry for entry in self.test_job["strategy"]["matrix"]["include"]}
        self.assertEqual(set(profiles), {"normal", "asan"})
        self.assertEqual(profiles["normal"]["sanitizer"], "")
        self.assertEqual(profiles["asan"]["sanitizer"], "asan")
        self.assertEqual(profiles["normal"]["name"], "Build & Test Suite")
        self.assertEqual(profiles["asan"]["name"], "ASan & UBSan Sanitizers")

    def test_unrealircd_uses_supported_quick_config(self):
        invocations = [line.strip() for line in self.run_scripts.splitlines() if line.strip().startswith("./Config")]
        self.assertEqual(invocations, ["./Config -quick"])
        self.assertIn('SANITIZER="${{ matrix.sanitizer }}"', self.run_scripts)
        self.assertIn('EXTRAPARA="--with-show-opermot=no"', self.run_scripts)

    def test_runtime_paths_are_portable(self):
        self.assertNotIn("/" + "home" + "/", self.run_scripts)
        self.assertNotIn("/" + "Users" + "/", self.run_scripts)
        self.assertNotRegex(self.run_scripts, re.compile(r"[A-Za-z]:\\Users\\"))
        self.assertIn('UDB_TEST_IRCD_ROOT=$RUNNER_TEMP/unrealircd', self.run_scripts)
        self.assertIn('UNREALIRCD_SOURCE=$RUNNER_TEMP/unrealircd-src', self.run_scripts)

    def test_deployment_has_production_only_gate(self):
        deploy = self.jobs["deploy"]
        self.assertEqual(deploy["environment"], "production")
        self.assertEqual(deploy["needs"], ["agentic", "release", "test"])
        self.assertEqual(deploy["if"], "github.event_name == 'push' && github.ref == 'refs/heads/main'")
        self.assertEqual(deploy["runs-on"], ["self-hosted", "linux", "x64", "udb-deploy"])
        for job_name in ("agentic", "release", "test"):
            self.assertEqual(self.jobs[job_name]["runs-on"], "ubuntu-latest")

    def test_deployment_uses_protected_host_local_command_not_ssh(self):
        steps = self.jobs["deploy"]["steps"]
        checkout = steps[0]
        self.assertEqual(checkout["uses"], "actions/checkout@v4")
        self.assertEqual(checkout["with"]["persist-credentials"], False)
        deploy_step = steps[-1]
        self.assertEqual(deploy_step["env"]["DEPLOY_COMMAND"], "${{ secrets.DEPLOY_COMMAND }}")
        self.assertIn('deploy_uid=$(id -u)', deploy_step["run"])
        self.assertIn('"$(stat -c %u -- "$command_path")" == "$deploy_uid"', deploy_step["run"])
        self.assertIn('"$DEPLOY_COMMAND" "$RELEASE_VERSION"', deploy_step["run"])
        self.assertNotIn("DEPLOY_SCRIPT", self.text)
        self.assertNotIn("ssh ", self.text)
        self.assertNotIn("scp ", self.text)
        self.assertNotIn("scripts/deploy-unrealircd.sh", self.text)
        self.assertNotIn("/var/run/docker.sock", self.text)
        self.assertNotIn("/home/", self.text)

    def test_runtime_and_sanitizer_builds_have_early_guards(self):
        self.assertIn('test -x "$UDB_TEST_IRCD_ROOT/bin/unrealircd"', self.run_scripts)
        self.assertIn("-fsanitize=address,undefined", self.run_scripts)
        self.assertIn("ldd \"$UDB_TEST_IRCD_ROOT/bin/unrealircd\"", self.run_scripts)
        self.assertIn("grep -q '__asan_'", self.run_scripts)

    def test_protocol_security_qualification_is_in_ci(self):
        for script in (
            "tests/test_security_hardening.py",
            "tests/test_anti_entropy.py",
            "tests/test_mutation_gap_recovery.py",
            "tests/test_protocol_fault_qualification.py",
        ):
            with self.subTest(script=script):
                self.assertIn(f"python3 {script}", self.run_scripts)


if __name__ == "__main__":
    unittest.main()
