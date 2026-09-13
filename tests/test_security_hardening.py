#!/usr/bin/env python3
"""Focused source contracts for protocol and diagnostic security hardening."""

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class SecurityHardeningContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = (ROOT / "src/udb_core.c.inc").read_text(encoding="utf-8")
        cls.mutation = (ROOT / "src/udb_mutation.c.inc").read_text(encoding="utf-8")
        cls.nicks = (ROOT / "src/udb_nicks.c.inc").read_text(encoding="utf-8")
        cls.protocol = (ROOT / "src/udb_protocol.c.inc").read_text(encoding="utf-8")
        cls.query = (ROOT / "src/udb_query.c.inc").read_text(encoding="utf-8")
        cls.channels = (ROOT / "src/udb_channels.c.inc").read_text(encoding="utf-8")
        cls.sync = (ROOT / "src/udb_sync.c.inc").read_text(encoding="utf-8")

    def test_uint64_wire_parsing_avoids_pointer_punning(self):
        self.assertIn("udb_parse_uint64_strict", self.core)
        self.assertNotIn("(unsigned long long *)&", self.protocol)
        self.assertIn("parc == 8 && !udb_parse_uint64_strict(parv[7], &watermark_seq)", self.protocol)
        self.assertIn("parc == 9 && !udb_parse_uint64_strict(parv[8], &watermark_seq)", self.protocol)

    def test_sha256_wire_digest_is_canonical_lowercase(self):
        self.assertIn("!isdigit(c) && (c < 'a' || c > 'f')", self.core)
        self.assertNotIn("tolower(c)", self.core)

    def test_reconciliation_can_adopt_a_restarted_stream(self):
        self.assertIn("candidate_authority_sid", (ROOT / "src/udb_internal.h").read_text(encoding="utf-8"))
        self.assertIn("ctx->last_applied_seq = udb_reconcile.watermark_seq", self.sync)
        self.assertGreaterEqual(self.sync.count("udb_reconcile.watermark_seq = 0"), 2)
        self.assertIn("strcmp(source_sid, ctx->authority_sid)", self.mutation)

    def test_expiry_relay_does_not_mint_an_intermediate_del(self):
        relay = self.mutation.index("udb_propagator_policy_present(ctx)")
        delete = self.mutation.index("udb_mutation_delete_local(ctx, block, line, direct_peer", relay)
        self.assertLess(relay, delete)
        self.assertIn("udb_send_db_to_one(selected.peer", self.mutation[relay:delete])
        high = self.mutation.rfind("high =", relay, delete)
        self.assertGreater(high, relay)

    def test_password_throttle_saturation_fails_closed(self):
        self.assertIn("int *saturated", self.nicks)
        self.assertIn("return saturated ||", self.nicks)
        self.assertNotIn("entry->since < oldest->since", self.nicks)

    def test_channel_keys_are_redacted_from_queries_and_debug(self):
        self.assertIn("!strcmp(rec->key, CKEY_MODES)", self.query)
        self.assertIn('strchr(modes, \'k\') ? "<redacted>" : parameters', self.channels)


if __name__ == "__main__":
    unittest.main()
