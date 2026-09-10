#!/usr/bin/env python3
"""Focused source contracts for the canonical Block K schema."""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BlockKSchemaContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/udb.h").read_text(encoding="utf-8")
        cls.core = (ROOT / "src/udb_core.c.inc").read_text(encoding="utf-8")
        cls.lines = (ROOT / "src/udb_lines.c.inc").read_text(encoding="utf-8")
        cls.store = (ROOT / "src/udb_store.c.inc").read_text(encoding="utf-8")
        cls.mutation = (ROOT / "src/udb_mutation.c.inc").read_text(encoding="utf-8")

    def test_non_f_profiles_have_reason_only_at_depth_three(self):
        block = re.search(r"/\* Block K accepts only leaf.*?\n\t}\n\n\t/\* Validate root", self.core, re.S).group(0)
        self.assertIn("if (depth != 3", block)
        self.assertNotIn("line_rec->data_str", self.lines)
        self.assertIn("udb_line_reason_valid", self.core)

    def test_g_s_and_z_are_unambiguous(self):
        self.assertIn("!(at = strchr(mask, '@'))", self.core)
        self.assertIn("strchr(at + 1, '@')", self.core)
        self.assertIn("inet_ntop", self.core)
        self.assertIn("CIDR identities are network identities", self.core)
        self.assertIn('tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, "*", pattern', self.lines)

    def test_f_has_one_encoding_and_exact_identity(self):
        self.assertIn("raw regex is not a second spelling", self.lines)
        self.assertIn("return !strcmp(left, right)", self.store)
        self.assertIn('!strcmp(parent->key, "F")', self.store)
        self.assertNotIn("KKEY_TYPE", self.header)

    def test_f_profile_is_explicit_and_native(self):
        for key in ("KKEY_MATCH_TYPE", "KKEY_TARGETS", "KKEY_ACTION", "KKEY_BAN_TIME"):
            self.assertIn(key, self.header)
        self.assertIn('"cpnNPqduatTR"', self.core)
        self.assertIn("MATCH_SIMPLE", self.core)
        self.assertIn("MATCH_PCRE_REGEX", self.core)
        self.assertIn("spamfilter_gettargets", self.lines)
        self.assertIn("ban_time", self.lines)
        self.assertIn("!banact_config_only", self.core)

    def test_invalid_complete_candidate_cannot_replace_live_rule(self):
        self.assertIn("udb_k_profile_valid(rec->parent)", self.mutation)
        self.assertIn("udb_k_tree_profiles_valid(session->tree)",
                      (ROOT / "src/udb_sync.c.inc").read_text(encoding="utf-8"))
        self.assertIn("udb_k_tree_profiles_valid(candidate)", self.core)
        self.assertIn("Compile before removing the old effect", self.lines)
        self.assertLess(self.lines.index("Compile before removing the old effect"),
                        self.lines.index("udb_line_remove_owned(type, pattern);", self.lines.index("Compile before removing the old effect")))

    def test_reserved_owner_marker_is_used_for_add_and_remove(self):
        self.assertIn('#define UDB_TKL_SET_BY "UDB:managed"', self.header)
        self.assertIn("strcmp(tkl->set_by, UDB_TKL_SET_BY)", self.lines)
        self.assertIn("UDB_TKL_SET_BY, expires", self.lines)


if __name__ == "__main__":
    unittest.main()
