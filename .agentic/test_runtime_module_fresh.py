"""Check the selected runtime module against this checkout, without installing it."""

from __future__ import annotations

import hashlib
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_CANDIDATES = (ROOT / "src" / "udb.so", ROOT / "dist" / "udb.so")


def selected_module() -> Path:
    override = os.environ.get("UDB_MODULE_PATH")
    candidates = ([Path(override)] if override else []) + list(MODULE_CANDIDATES)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError("No compiled module; build src/udb.so or set UDB_MODULE_PATH")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


class RuntimeModuleFreshnessTest(unittest.TestCase):
    def test_selected_module_matches_build(self):
        expected = next((path for path in MODULE_CANDIDATES if path.is_file()), None)
        self.assertIsNotNone(expected,
            "No checkout build; build with make custommodule MODULEFILE=udb/src/udb")
        selected = selected_module()

        newest_source = max(
            (path.stat().st_mtime for path in (ROOT / "src").rglob("*") if path.suffix in (".c", ".h", ".inc")),
            default=0.0,
        )
        self.assertLessEqual(
            newest_source,
            expected.stat().st_mtime,
            f"Stale build: {expected} is older than source. Rebuild the checkout module",
        )
        self.assertEqual(
            sha256(expected),
            sha256(selected),
            f"Selected module differs from the checkout build; set UDB_MODULE_PATH={expected}",
        )


if __name__ == "__main__":
    unittest.main()
