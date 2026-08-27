from __future__ import annotations

import importlib.util
import tempfile
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

    def test_all_wrappers_start_with_frontmatter(self):
        for path, content in self.expected.items():
            with self.subTest(path=path):
                self.assertTrue(content.startswith("---\n"))
                generate.parse_frontmatter(content, path)

    def test_opencode_permission_contract(self):
        for path, content in self.expected.items():
            if path.parent != generate.OC_ROOT:
                continue
            with self.subTest(path=path):
                frontmatter = generate.parse_frontmatter(content, path)
                self.assertEqual(frontmatter["mode"], "primary")
                self.assertNotIn("permissions", frontmatter)
                self.assertEqual(frontmatter["permission"]["task"], "deny")
                self.assertEqual(frontmatter["permission"]["skill"]["*"], "deny")

    def test_reviewer_is_read_only_in_both_clis(self):
        ag_path = generate.AG_ROOT / "udb-reviewer" / "agent.md"
        ag_frontmatter = generate.parse_frontmatter(self.expected[ag_path], ag_path)
        self.assertNotIn("replace_file_content", ag_frontmatter["tools"])
        self.assertNotIn("run_command", ag_frontmatter["tools"])

        oc_path = generate.OC_ROOT / "udb-reviewer.md"
        oc_frontmatter = generate.parse_frontmatter(self.expected[oc_path], oc_path)
        self.assertEqual(oc_frontmatter["permission"]["edit"], "deny")
        self.assertEqual(oc_frontmatter["permission"]["bash"], "deny")

    def test_unexpected_generated_files_are_detected(self):
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
                generate.unexpected_files({}, ag_root, oc_root),
                {extra_ag, extra_oc},
            )


if __name__ == "__main__":
    unittest.main()
