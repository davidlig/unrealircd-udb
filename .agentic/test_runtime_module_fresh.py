from __future__ import annotations

import hashlib
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Must mirror tests/ module resolution: UDB_MODULE_PATH overrides, then the
# freshly built module, then the bundled one.
MODULE_CANDIDATES = (
    ROOT / "src" / "udb.so",
    ROOT / "dist" / "udb.so",
)


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
    """Runtime/integration tests load the module installed under the test
    UnrealIRCd tree, not the freshly built one. A stale installed .so makes
    tests validate old behavior, so the installed copy must match the build."""

    def test_installed_module_matches_build(self):
        expected = next((path for path in MODULE_CANDIDATES if path.is_file()), None)
        if expected is None:
            self.skipTest("No built module found (src/udb.so); build with: make custommodule MODULEFILE=udb/src/udb")

        installed = installed_module()
        if not installed.is_file():
            self.skipTest(f"No installed module at {installed}; runtime tests would SKIP anyway")

        built_hash = sha256(expected)
        installed_hash = sha256(installed)
        self.assertEqual(
            built_hash,
            installed_hash,
            f"Stale installed module: {installed} differs from {expected}.\n"
            f"Refresh it and re-run the tests:\n"
            f"  cp {expected} {installed}\n"
            f"Or point the tests at the fresh build:\n"
            f"  UDB_MODULE_PATH={expected}",
        )


if __name__ == "__main__":
    unittest.main()
