#!/usr/bin/env python3
"""Low-churn static contracts and unrelated native-+k fixes.

Critical Block N semantics are proved by runtime tests
(tests/runtime_nick_auth.py) and the independent ownership model
(tests/test_block_n_ownership_model.py). This file intentionally keeps only
low-churn static invariants:

  * legacy auth concepts must stay gone;
  * identity and effect ownership must remain separate markers;
  * local ModData must never be reached through client->local->passwd;
  * the NICK anti-flood/remote guard and the generic native +k fix.
"""

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
        cls.effects = (ROOT / "src/udb_effects.c.inc").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

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

    def test_effect_ownership_is_a_separate_explicit_marker(self):
        self.assertIn("UdbNickEffects", self.nicks)
        self.assertIn('mreq.name = "udb_nick_effects";', self.nicks)
        self.assertNotIn("udb_nick_strip", self.nicks)
        for helper in (
            "udb_nick_effects_apply_modes",
            "udb_nick_effects_apply_vhost",
            "udb_nick_effects_apply_snomasks",
            "udb_nick_effects_revoke_modes",
            "udb_nick_effects_revoke_vhost",
            "udb_nick_effects_revoke_snomasks",
            "udb_nick_revoke_effects",
            "udb_nick_revoke_identity",
        ):
            self.assertIn(helper, self.nicks)
        revoke = re.search(r"static void udb_nick_revoke_effects\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn("udb_nick_revoke_oper(client);", revoke)
        self.assertIn("udb_nick_effects_revoke_modes(client);", revoke)
        self.assertIn("udb_nick_effects_revoke_vhost(client);", revoke)
        self.assertIn("udb_nick_effects_revoke_snomasks(client);", revoke)

    def test_identity_revocation_only_owns_account_and_regnick(self):
        revoke = re.search(r"static void udb_nick_revoke_identity\(.*?\n}\n", self.nicks, re.S).group(0)
        self.assertIn('strlcpy(client->user->account, "*"', revoke)
        self.assertIn("UMODE_REGNICK", revoke)
        self.assertNotIn("udb_nick_effects_revoke", revoke)
        self.assertNotIn("udb_nick_remove_vhost", revoke)

    def test_password_cache_never_uses_local_passwd(self):
        take = re.search(r"static UdbNickPasswordCache \*udb_nick_password_cache_take\(.*?\n}\n", self.nicks,
                         re.S).group(0)
        self.assertIn("m->ptr = NULL;", take)
        self.assertIn("return cache;", take)
        self.assertNotIn("client->local->passwd", self.nicks)
        override = re.search(r"CMD_OVERRIDE_FUNC\(udb_override_nick\).*?\n}\n\nstatic int udb_hook_can_use_nick",
                             self.nicks, re.S).group(0)
        # Local-only ModData must never be touched for remote NICK commands.
        self.assertIn("if (parc <= 1 || !MyConnect(client))", override)
        self.assertLess(override.index("!MyConnect(client)"),
                        override.index("udb_nick_password_cache_clear(client);"))

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
        self.assertIn("UDB_NICK_APPLY_UNSUSPEND", internal)
        self.assertIn("static void udb_nick_apply(Client *client, UdbRecord *nick_rec, UdbNickApplyReason reason);",
                      internal)
        self.assertNotIn("int is_hot_sync", internal)
        self.assertIn("UdbNickApplyReason reason = is_new ? UDB_NICK_APPLY_REFRESH : UDB_NICK_APPLY_ADOPT;",
                      self.effects)
        self.assertIn("reason = UDB_NICK_APPLY_UNSUSPEND;", self.effects)

    def test_unsuspend_rename_has_a_dedicated_notice_path(self):
        self.assertIn("candidate_nick_profile->nick_unsuspend_transition = 1;", self.mutation)
        self.assertIn("candidate->nick_unsuspend_transition = 1;", self.nicks)
        self.assertIn("udb_nick_force_rename_after_unsuspend(client, nick_rec->key);", self.nicks)
        helper = re.search(r"static void udb_nick_force_rename_after_unsuspend\(.*?\n}\n", self.nicks,
                           re.S).group(0)
        self.assertIn("NULL, 0, 1", helper)

    def test_generic_nick_modes_remain_unfiltered(self):
        self.assertIn("udb_nick_effects_apply_modes(client, effect_rec);", self.nicks)
        self.assertNotRegex(self.nicks, r"NKEY_MODES[^\n]*['\"]S['\"]")

    def test_persisted_key_fallback_requires_no_native_key(self):
        join = re.search(r"static int udb_hook_can_join\(.*?\n}\n\nstatic void handle_join", self.channels, re.S).group(0)
        self.assertIn("!cm_getparameter(channel, 'k')", join)
        self.assertRegex(join, r"channel->users == 0 && !cm_getparameter\(channel, 'k'\) && !is_founder")
        self.assertIn("strcmp(key, configured_key)", join)

    def test_validation_log_identifies_the_rejected_canonical_profile(self):
        self.assertIn('block->letter == UDB_BLOCK_NICKS ? "N" : "K"', self.mutation)
        self.assertIn("invalid complete $profile_kind profile", self.mutation)

    def test_ci_runs_ownership_contracts(self):
        self.assertIn("python3 tests/test_hash_index_contract.py", self.workflow)
        self.assertIn("python3 tests/test_block_n_ownership_model.py", self.workflow)
        self.assertIn("python3 tests/runtime_nick_auth.py", self.workflow)
        self.assertIn("MODULEFILE=udb/tests/udb_test_state", self.workflow)
        self.assertNotIn("runtime_nick_suspend_auth.py", self.workflow)


if __name__ == "__main__":
    unittest.main()
