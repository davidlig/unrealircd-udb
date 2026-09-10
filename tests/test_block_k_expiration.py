#!/usr/bin/env python3
"""Focused contract checks for absolute, authority-mediated Block K expiry."""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BlockKExpiryContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/udb.h").read_text(encoding="utf-8")
        cls.core = (ROOT / "src/udb_core.c.inc").read_text(encoding="utf-8")
        cls.lines = (ROOT / "src/udb_lines.c.inc").read_text(encoding="utf-8")
        cls.mutation = (ROOT / "src/udb_mutation.c.inc").read_text(encoding="utf-8")
        cls.protocol = (ROOT / "src/udb_protocol.c.inc").read_text(encoding="utf-8")
        cls.sync = (ROOT / "src/udb_sync.c.inc").read_text(encoding="utf-8")

    def test_duration_is_not_a_k_schema_alias(self):
        for text in (self.header, self.core, self.lines, (ROOT / "src/udb_store.c.inc").read_text(encoding="utf-8")):
            self.assertNotIn("KKEY_DURATION", text)
            self.assertNotIn('"duration"', text)
        self.assertIn('#define KKEY_EXPIRES "expires"', self.header)

    def test_expires_is_strict_absolute_timestamp(self):
        self.assertIsNotNone(re.search(r"udb_k_expires_valid\(.*?expires > 0", self.core, re.S))
        self.assertIn("udb_parse_time_t(value + 1, &expires)", self.core)
        self.assertIn("expires <= TStime()", self.lines)
        self.assertNotIn("udb_time_add(TStime()", self.lines)

    def test_expiry_is_swept_and_authority_deleted_transactionally(self):
        self.assertIn("udb_lines_expiry_sweep(now)", self.sync)
        self.assertIn("udb_mutation_expire_local", self.lines)
        self.assertIn("udb_mutation_delete_local", self.mutation)
        self.assertIn("udb_file_write_snapshot", self.mutation)
        self.assertIn('":%s DB * DEL %s"', self.mutation)
        self.assertIn("udb_peer_authorizes_us(direct_peer)", self.mutation)

    def test_exp_is_compare_and_delete_and_not_broadcast(self):
        self.assertIn("expires->data_num != (unsigned long)expected_expires", self.mutation)
        self.assertIn("expected_expires > TStime()", self.mutation)
        self.assertIn("is_broadcast", self.mutation)
        self.assertIsNotNone(re.search(r'!strcasecmp\(subcmd, "EXP"\).*?udb_mutation_exp', self.protocol, re.S))

    def test_runtime_is_not_the_expiry_source_of_truth(self):
        self.assertIn("udb_line_remove_owned(type, pattern)", self.lines)
        self.assertIn("udb_line_expiry_pending_add", self.lines)
        self.assertIn("UDB_K_EXP_REQUEST", self.lines)
        self.assertIn("UDB_K_EXP_STALE", self.mutation)

    def test_k_validation_hardening(self):
        self.assertIn("udb_zline_mask_valid", self.core)
        self.assertIn("udb_spamfilter_regex_valid", self.core)
        self.assertIn("!banact_config_only(action)", self.core)


if __name__ == "__main__":
    unittest.main()
