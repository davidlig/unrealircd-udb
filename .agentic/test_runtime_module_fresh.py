from __future__ import annotations

import hashlib
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_CANDIDATES = (ROOT / "src" / "udb.so", ROOT / "dist" / "udb.so")


def runtime_root() -> Path:
    override = os.environ.get("UDB_TEST_IRCD_ROOT")
    return Path(override) if override else Path.home() / "unrealircd"


def installed_module() -> Path:
    override = os.environ.get("UDB_MODULE_PATH")
    if override:
        return Path(override)
    return runtime_root() / "modules" / "third" / "udb.so"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


class RuntimeModuleFreshnessTest(unittest.TestCase):
    def test_installed_module_matches_build(self):
        expected = next((path for path in MODULE_CANDIDATES if path.is_file()), None)
        if expected is None:
            self.skipTest("No built module found; build with make custommodule MODULEFILE=udb/src/udb")

        installed = installed_module()
        if not installed.is_file():
            self.skipTest(f"No installed module at {installed}; runtime tests would skip")

        newest_source = max(
            (path.stat().st_mtime for path in (ROOT / "src").rglob("*") if path.suffix in (".c", ".h", ".inc")),
            default=0.0,
        )
        self.assertLessEqual(
            newest_source,
            expected.stat().st_mtime,
            f"Stale build: {expected} is older than source. Rebuild then copy it to {installed}",
        )
        self.assertEqual(
            sha256(expected),
            sha256(installed),
            f"Stale installed module: copy {expected} to {installed} or set UDB_MODULE_PATH={expected}",
        )


if __name__ == "__main__":
    unittest.main()
