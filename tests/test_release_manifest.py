#!/usr/bin/env python3
"""Contracts for selecting a compatible UnrealIRCd Stable release."""

import importlib.util
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parent.parent / "scripts" / "resolve_unrealircd_release.py"
SPEC = importlib.util.spec_from_file_location("resolve_unrealircd_release", MODULE)
resolver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resolver)


def manifest(version="6.2.7", source_url=None, kind="Stable"):
    return {
        "6.0": {
            "Stable": {
                "type": kind,
                "version": version,
                "downloads": {
                    "src": source_url
                    or f"https://www.unrealircd.org/downloads/unrealircd-{version}.tar.gz"
                },
            }
        }
    }


class ReleaseManifestTests(unittest.TestCase):
    def test_selects_supported_stable(self):
        self.assertEqual(
            resolver.select_release(manifest()),
            ("6.2.7", "https://www.unrealircd.org/downloads/unrealircd-6.2.7.tar.gz"),
        )

    def test_rejects_other_series(self):
        with self.assertRaisesRegex(ValueError, "unsupported"):
            resolver.select_release(manifest(version="6.3.0"))

    def test_rejects_non_stable(self):
        with self.assertRaisesRegex(ValueError, "not Stable"):
            resolver.select_release(manifest(kind="Development"))

    def test_rejects_untrusted_source(self):
        with self.assertRaisesRegex(ValueError, "source URL"):
            resolver.select_release(manifest(source_url="https://example.org/unrealircd-6.2.7.tar.gz"))

    def test_rejects_missing_fields(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            resolver.select_release({"6.0": {"Stable": {"version": "6.2.7"}}})


if __name__ == "__main__":
    unittest.main()
