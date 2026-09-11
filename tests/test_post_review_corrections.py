#!/usr/bin/env python3
"""Focused contracts for the post-review suspend and native-+k fixes."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PostReviewCorrectionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.nicks = (ROOT / "src/udb_nicks.c.inc").read_text(encoding="utf-8")
        cls.channels = (ROOT / "src/udb_channels.c.inc").read_text(encoding="utf-8")
        cls.mutation = (ROOT / "src/udb_mutation.c.inc").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def test_suspend_never_uses_usermode_s_as_state(self):
        strip = re.search(r"static void udb_nick_strip\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertNotIn('set_usermode("S")', strip)
        self.assertNotRegex(strip, r"UMODE_.*S")
        suspend = re.search(r"if \(suspend\).*?\n\t}\n\n\tif \(client->user\)", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_strip(client, nick_rec);", suspend)
        self.assertNotIn('set_usermode("S")', suspend)

    def test_generic_nick_modes_remain_unfiltered(self):
        self.assertIn("udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);", self.nicks)
        self.assertNotRegex(self.nicks, r"NKEY_MODES[^\n]*['\"]S['\"]")

    def test_persisted_key_fallback_requires_no_native_key(self):
        join = re.search(r"static int udb_hook_can_join\(.*?\n}\n\nstatic void handle_join", self.channels, re.S).group(0)
        self.assertIn("!cm_getparameter(channel, 'k')", join)
        self.assertRegex(join, r"channel->users == 0 && !cm_getparameter\(channel, 'k'\) && !is_founder")
        self.assertIn("strcmp(key, configured_key)", join)

    def test_validation_log_identifies_the_rejected_canonical_profile(self):
        self.assertIn('block->letter == UDB_BLOCK_NICKS ? "N" : "K"', self.mutation)
        self.assertIn("invalid complete $profile_kind profile", self.mutation)

    def test_ci_runs_hash_and_post_review_contracts(self):
        self.assertIn("python3 tests/test_hash_index_contract.py", self.workflow)
        self.assertIn("python3 tests/test_post_review_corrections.py", self.workflow)


if __name__ == "__main__":
    unittest.main()
