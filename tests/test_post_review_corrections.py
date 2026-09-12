#!/usr/bin/env python3
"""Focused contracts for the post-review nick identity and native-+k fixes."""

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
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks, re.S).group(0)
        adopt_suspend = re.search(r"if \(reason == UDB_NICK_APPLY_REFRESH && suspend\)\n\t\{.*?\n\t}\n\n\t/\* A protected profile",
                                  apply, re.S).group(0)
        self.assertIn("udb_nick_identity_clear(client);", adopt_suspend)
        self.assertNotIn("udb_nick_force_rename", adopt_suspend)
        self.assertNotIn('set_usermode("S")', adopt_suspend)

    def test_suspend_auth_symbols_are_gone_and_replaced_by_identity(self):
        for banned in (
            "UdbNickSuspendAuth",
            "udb_nick_suspend_auth_",
            "saved_auth",
            "matching_account",
            "newly_protected",
        ):
            self.assertNotIn(banned, self.nicks, f"legacy concept survived: {banned}")
            self.assertNotIn(banned, self.mutation, f"legacy concept survived: {banned}")
            self.assertNotIn(banned, self.core, f"legacy concept survived: {banned}")
        self.assertIn("UdbNickIdentity", self.nicks)
        self.assertIn('mreq.name = "udb_nick_identity";', self.nicks)
        for helper in ("udb_nick_identity_free", "udb_nick_identity_clear", "udb_nick_identity_set",
                       "udb_nick_identity_valid", "udb_nick_identity_owned"):
            self.assertIn(f"static", self.nicks)
            self.assertIn(helper, self.nicks)
        take = re.search(r"static UdbNickPasswordCache \*udb_nick_password_cache_take\(.*?\n}\n", self.nicks,
                         re.S).group(0)
        self.assertIn("m->ptr = NULL;", take)
        self.assertIn("return cache;", take)
        self.assertNotIn("client->local->passwd", self.nicks)

    def test_pending_is_consumed_into_identity_only_after_confirmation(self):
        post = re.search(r"static int udb_hook_post_nick_change\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_pending_auth_consume(client, new_rec)", post)
        self.assertIn("udb_nick_identity_set(client, new_rec);", post)
        self.assertIn("udb_nick_apply(client, new_rec, UDB_NICK_APPLY_ADOPT);", post)
        self.assertLess(post.index("udb_nick_pending_auth_consume(client, new_rec)"),
                        post.index("udb_nick_identity_set(client, new_rec);"))
        self.assertLess(post.index("udb_nick_identity_set(client, new_rec);"),
                        post.index("udb_nick_apply(client, new_rec, UDB_NICK_APPLY_ADOPT);"))
        connect = re.search(r"static int udb_hook_local_connect\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_pending_auth_consume(client, nick_rec)", connect)
        self.assertIn("udb_nick_identity_set(client, nick_rec);", connect)
        self.assertIn("udb_nick_apply(client, nick_rec, UDB_NICK_APPLY_ADOPT);", connect)
        consume = re.search(r"static int udb_nick_pending_auth_consume\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_pending_auth_clear(client);", consume)
        self.assertNotIn("identity_set", consume)

    def test_can_use_only_records_pending_auth(self):
        can_use = re.search(r"static int udb_hook_can_use_nick\(.*?\n}\n\nstatic int udb_hook_nick_change",
                            self.nicks, re.S).group(0)
        self.assertIn("udb_nick_password_cache_take(client, newnick)", can_use)
        self.assertIn("udb_nick_pending_auth_set(client, nick_rec);", can_use)
        self.assertNotIn("udb_nick_identity_set", can_use)
        override = re.search(r"CMD_OVERRIDE_FUNC\(udb_override_nick\).*?\n}\n\nstatic int udb_hook_can_use_nick",
                             self.nicks, re.S).group(0)
        self.assertIn("udb_nick_password_cache_set(client, clean_nick, pass, validated);", override)
        self.assertNotIn("client->local->passwd", override)
        # Local-only ModData must never be touched for remote NICK commands.
        self.assertIn("if (parc <= 1 || !MyConnect(client))", override)
        self.assertLess(override.index("!MyConnect(client)"),
                        override.index("udb_nick_password_cache_clear(client);"))

    def test_passless_nick_use_skips_password_and_recovery(self):
        can_use = re.search(r"static int udb_hook_can_use_nick\(.*?\n}\n\nstatic int udb_hook_nick_change",
                            self.nicks, re.S).group(0)
        override = re.search(r"CMD_OVERRIDE_FUNC\(udb_override_nick\).*?\n}\n\nstatic int udb_hook_can_use_nick",
                             self.nicks, re.S).group(0)
        ghost = re.search(r"CMD_FUNC\(cmd_ghost\).*?\n}\n\nCMD_OVERRIDE_FUNC", self.nicks, re.S).group(0)
        self.assertIn("if (!pass_rec)", can_use)
        self.assertIn("Access to this nickname is not permitted from your IP address.", can_use)
        self.assertLess(can_use.index("if (!pass_rec)"), can_use.index("udb_nick_password_cache_take"))
        self.assertIn("goto dispatch_clean_nick;", override)
        self.assertIn("Nick %s has no UDB password.", ghost)
        self.assertNotIn("NKEY_CHALLENGE", self.nicks)

    def test_apply_adopt_requires_active_identity_and_refresh_suspend_keeps_nick(self):
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks,
                          re.S).group(0)
        self.assertIn("if (!udb_nick_identity_valid(client, nick_rec))", apply)
        guard = apply.index("if (!udb_nick_identity_valid(client, nick_rec))")
        refresh_suspend = apply.index("if (reason == UDB_NICK_APPLY_REFRESH && suspend)")
        self.assertLess(refresh_suspend, guard)
        # The guard strips UDB-owned effects, clears identity, and renames.
        guard_block = apply[guard:apply.index("if (suspend)", guard)]
        self.assertIn("udb_nick_strip(client, nick_rec);", guard_block)
        self.assertIn("udb_nick_identity_clear(client);", guard_block)
        self.assertIn("udb_nick_force_rename(client, nick_rec->key);", guard_block)
        # REFRESH + suspend clears identity and keeps the nick without rename.
        refresh_block = apply[refresh_suspend:guard]
        self.assertIn("udb_nick_strip(client, nick_rec);", refresh_block)
        self.assertIn("udb_nick_identity_clear(client);", refresh_block)
        self.assertNotIn("udb_nick_force_rename", refresh_block)
        # ADOPT of a suspended profile still requires the preflight credential:
        # identity_valid runs before the final suspend branch.
        final_suspend = apply.index("if (suspend)", guard)
        self.assertLess(guard, final_suspend)
        adopt_block = apply[final_suspend:apply.index("if (client->user)", final_suspend)]
        self.assertIn("udb_nick_identity_clear(client);", adopt_block)
        self.assertNotIn("udb_nick_force_rename", adopt_block)

    def test_forbid_and_access_denial_strip_owned_effects_only(self):
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks,
                          re.S).group(0)
        for prefix, marker in (("forbid", "This nickname is forbidden. Reason: %s"),
                               ("access", "Access to %s is not permitted from your IP address.")):
            self.assertIn(marker, apply)
        self.assertGreaterEqual(apply.count("if (udb_nick_identity_owned(client, nick_rec->key))"), 4)

    def test_passless_branch_never_owns_identity_or_effects(self):
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks,
                          re.S).group(0)
        no_pass = re.search(r"if \(!pass_rec\)\n\t\{.*?\n\t}\n", apply, re.S).group(0)
        self.assertIn("udb_nick_identity_clear(client);", no_pass)
        self.assertNotIn("udb_nick_strip(client, nick_rec);", no_pass)
        self.assertNotIn("strlcpy(client->user->account", no_pass)
        self.assertLess(apply.index("if (!pass_rec)"), apply.index("strlcpy(client->user->account"))

    def test_remove_record_distinguishes_identity_from_dormant_profiles(self):
        remove = re.search(r"static void udb_nick_remove_record\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("else if (udb_nick_identity_owned(client, nick_rec->key))", remove)
        self.assertIn("if (udb_nick_identity_owned(client, nick_rec->key))", remove)
        self.assertIn("udb_nick_identity_clear(client);", remove)
        self.assertNotIn("udb_nick_profile_has_pass(nick_rec)", remove)
        # A full replacement keeps active identity only when the candidate is
        # equivalent; otherwise it strips owned effects and clears the marker.
        self.assertIn("udb_nick_replacement_tree", remove)
        self.assertIn("udb_nick_identity_valid(client, candidate)", remove)
        self.assertIn("NKEY_FORBID, candidate", remove)
        self.assertIn("udb_nick_strip(client, rec);", remove)

    def test_nick_change_destroys_identity_only_for_owned_effects(self):
        change = re.search(r"static int udb_hook_nick_change\(.*?\n}\n\nstatic int udb_hook_post_nick_change",
                           self.nicks, re.S).group(0)
        self.assertIn("udb_nick_identity_owned(client, old_rec->key)", change)
        self.assertIn("udb_nick_identity_clear(client);", change)
        self.assertNotIn("udb_nick_pending_auth_clear", change)
        self.assertLess(change.index("udb_nick_identity_owned(client, old_rec->key)"),
                        change.index("udb_nick_strip(client, old_rec);"))

    def test_passless_to_pass_never_trusts_external_account(self):
        insert = re.search(r"static void udb_mutation_ins\(.*?\n}\n\nstatic int udb_mutation_delete_local",
                           self.mutation, re.S).group(0)
        self.assertIn("nick_pass_added", insert)
        self.assertIn("udb_nick_identity_clear(profile_client);", insert)
        self.assertIn("udb_nick_pending_auth_clear(profile_client);", insert)
        self.assertNotIn("udb_nick_strip(profile_client, rec->parent);", insert)
        apply = re.search(r"static void udb_nick_apply\(.*?\n}\n\nstatic void udb_nick_strip", self.nicks,
                          re.S).group(0)
        self.assertIn("if (!udb_nick_identity_valid(client, nick_rec))", apply)

    def test_del_reapplies_candidate_profile_and_preserves_replacement_context(self):
        delete = re.search(r"static int udb_mutation_delete_local\(.*?\n}\n\nstatic void udb_mutation_del",
                           self.mutation, re.S).group(0)
        self.assertIn("candidate_nick_profile = candidate_rec->parent;", delete)
        self.assertIn("udb_apply_special_record(ctx, block, candidate_nick_profile, 1);", delete)
        self.assertIn("udb_nick_prepare_tree_replace(block, session->tree);", self.core)
        self.assertIn("udb_nick_finish_tree_replace();", self.core)
        self.assertNotIn("udb_nick_suspend_auth_prepare_tree_replace", self.core)

    def test_pending_auth_expires_after_the_attempt(self):
        override = re.search(r"CMD_OVERRIDE_FUNC\(udb_override_nick\).*?\n}\n\nstatic int udb_hook_can_use_nick",
                             self.nicks, re.S).group(0)
        self.assertIn("udb_nick_pending_auth_expire(client, clean_nick);", override)
        self.assertIn("udb_nick_pending_auth_expire(client, parc > 1 ? parv[1] : NULL);", override)
        expire = re.search(r"static void udb_nick_pending_auth_expire\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("!MyConnect(client)", expire)
        self.assertIn("!IsUser(client)", expire)
        self.assertIn("strcasecmp(client->name, requested_nick)", expire)

    def test_apply_reason_enum_replaces_hot_sync_boolean(self):
        internal = (ROOT / "src/udb_internal.h").read_text(encoding="utf-8")
        self.assertIn("UDB_NICK_APPLY_ADOPT", internal)
        self.assertIn("UDB_NICK_APPLY_REFRESH", internal)
        self.assertIn("static void udb_nick_apply(Client *client, UdbRecord *nick_rec, UdbNickApplyReason reason);",
                      internal)
        self.assertNotIn("int is_hot_sync", internal)
        effects = (ROOT / "src/udb_effects.c.inc").read_text(encoding="utf-8")
        self.assertIn("is_new ? UDB_NICK_APPLY_REFRESH : UDB_NICK_APPLY_ADOPT", effects)

    def test_generic_nick_modes_remain_unfiltered(self):
        self.assertIn("udb_nick_set_modes(client, nick_rec, effect_rec, effect_rec->data_str);", self.nicks)
        self.assertNotRegex(self.nicks, r"NKEY_MODES[^\n]*['\"]S['\"]")

    def test_persisted_key_fallback_requires_no_native_key(self):
        join = re.search(r"static int udb_hook_can_join\(.*?\n}\n\nstatic void handle_join", self.channels, re.S).group(0)
        self.assertIn("!cm_getparameter(channel, 'k')", join)
        self.assertRegex(join, r"channel->users == 0 && !cm_getparameter\(channel, 'k'\) && !is_founder")
        self.assertIn("strcmp(key, configured_key)", join)

    def test_validation_log_identifies_the_rejected_canonical_profile(self):
        self.assertIn('block->letter == UDB_BLOCK_NICKS ? "N" : "K"', self.mutation)
        self.assertIn("invalid complete $profile_kind profile", self.mutation)

    def test_ci_runs_hash_and_nick_auth_contracts(self):
        self.assertIn("python3 tests/test_hash_index_contract.py", self.workflow)
        self.assertIn("python3 tests/test_post_review_corrections.py", self.workflow)
        self.assertIn("python3 tests/runtime_nick_auth.py", self.workflow)
        self.assertNotIn("runtime_nick_suspend_auth.py", self.workflow)


if __name__ == "__main__":
    unittest.main()
