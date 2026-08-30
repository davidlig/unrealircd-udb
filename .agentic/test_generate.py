from __future__ import annotations

import importlib.util
import json
import tempfile
import tomllib
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("generate.py")
SPEC = importlib.util.spec_from_file_location("agentic_generate", MODULE_PATH)
generate = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(generate)


class GeneratorContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = generate.load_config()
        generate.validate(cls.config)
        cls.expected = generate.expected_files(cls.config)
        generate.validate_generated(cls.expected)

    def test_instruction_budgets(self):
        agents = generate.ROOT / "AGENTS.md"
        self.assertLessEqual(agents.stat().st_size, 4096)
        for name, role in self.config["roles"].items():
            with self.subTest(role=name):
                self.assertLessEqual(len(role["prompt"]), 700)
                self.assertLessEqual(len(role["description"]), 180)

    def test_antigravity_contract(self):
        for path, content in self.expected.items():
            if generate.AG_ROOT not in path.parents or path.name != "agent.md":
                continue
            with self.subTest(path=path):
                fm = generate.parse_frontmatter(content, path)
                self.assertTrue(fm["mainAgent"])
                self.assertFalse(fm["subagent"])
                self.assertEqual(fm["model"], "flash")
                self.assertNotIn("manage_task", fm["tools"])
                self.assertNotIn("invoke_subagent", fm["tools"])

    def test_opencode_contract(self):
        for path, content in self.expected.items():
            if path.parent != generate.OC_ROOT:
                continue
            with self.subTest(path=path):
                fm = generate.parse_frontmatter(content, path)
                self.assertEqual(fm["mode"], "primary")
                self.assertEqual(fm["permission"]["task"], "deny")
                self.assertEqual(fm["permission"]["skill"]["*"], "deny")

        cfg = json.loads(self.expected[generate.OPENCODE_CONFIG])
        self.assertEqual(cfg["default_agent"], "udb-developer")
        self.assertEqual(cfg["subagent_depth"], 0)
        self.assertNotIn("model", cfg)

    def test_codex_contract(self):
        cfg = tomllib.loads(self.expected[generate.CODEX_CONFIG])
        self.assertEqual(cfg["model"], "gpt-5.6-terra")
        self.assertEqual(cfg["model_reasoning_effort"], "medium")
        self.assertEqual(cfg["model_verbosity"], "low")
        self.assertFalse(cfg["features"]["multi_agent"])

    def test_reviewer_is_read_only(self):
        ag_path = generate.AG_ROOT / "udb-reviewer" / "agent.md"
        ag_fm = generate.parse_frontmatter(self.expected[ag_path], ag_path)
        self.assertNotIn("replace_file_content", ag_fm["tools"])
        self.assertNotIn("run_command", ag_fm["tools"])

        oc_path = generate.OC_ROOT / "udb-reviewer.md"
        oc_fm = generate.parse_frontmatter(self.expected[oc_path], oc_path)
        self.assertEqual(oc_fm["permission"]["edit"], "deny")
        self.assertEqual(oc_fm["permission"]["bash"], "deny")

    def test_unexpected_generated_agents_are_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ag_root = root / ".agents" / "agents"
            oc_root = root / ".opencode" / "agents"
            extra_ag = ag_root / "obsolete" / "agent.md"
            extra_oc = oc_root / "obsolete.md"
            extra_ag.parent.mkdir(parents=True)
            extra_oc.parent.mkdir(parents=True)
            extra_ag.write_text("obsolete", encoding="utf-8")
            extra_oc.write_text("obsolete", encoding="utf-8")
            self.assertEqual(
                generate.unexpected_agent_files({}, ag_root, oc_root),
                {extra_ag, extra_oc},
            )


if __name__ == "__main__":
    unittest.main()
