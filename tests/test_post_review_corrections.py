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
        cls.core = (ROOT / "src/udb_core.c.inc").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def test_suspend_never_uses_usermode_s_as_state(self):
        strip = re.search(r"static void udb_nick_strip\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertNotIn('set_usermode("S")', strip)
        self.assertNotRegex(strip, r"UMODE_.*S")
        suspend = re.search(r"if \(suspend\).*?\n\t}\n\n\tif \(client->user\)", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_strip(client, nick_rec);", suspend)
        self.assertNotIn('set_usermode("S")', suspend)

    def test_suspend_auth_is_separate_and_password_cache_stays_one_shot(self):
        self.assertIn("udb_nick_suspend_auth", self.nicks)
        self.assertIn("udb_nick_suspend_auth_md", self.nicks)
        take = re.search(r"static int udb_nick_password_cache_take\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_password_cache_clear(client);", take)
        self.assertIn("return valid;", take)

    def test_suspend_removal_reapplies_the_candidate_profile(self):
        suspend_remove = re.search(r"else if \(!strcmp\(rec->key, NKEY_SUSPEND\)\).*?\n\t\t\t}\n", self.nicks, re.S).group(0)
        self.assertNotIn("Please identify again", suspend_remove)
        self.assertNotIn("udb_nick_force_rename", suspend_remove)
        delete = re.search(r"static int udb_mutation_delete_local\(.*?\n}\n\nstatic void udb_mutation_del", self.mutation, re.S).group(0)
        self.assertIn("candidate_nick_profile", delete)
        self.assertIn("udb_apply_special_record(ctx, block, candidate_nick_profile, 1);", delete)
        self.assertIn("udb_nick_suspend_auth_prepare_tree_replace(block, session->tree);", self.core)
        self.assertIn("udb_nick_suspend_auth_prepare_tree_replace(block, NULL);", self.core)

    def test_hot_sync_accepts_valid_saved_suspend_auth(self):
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_suspend_auth_valid(client, nick_rec)", apply)
        self.assertIn("matching_account || saved_auth", apply)

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
