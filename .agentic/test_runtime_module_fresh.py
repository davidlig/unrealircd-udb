"""Check the selected runtime module against this checkout, without installing it."""

from __future__ import annotations

import hashlib
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_EXTENSION = ".dll" if os.name == "nt" else ".so"
MODULE_CANDIDATES = (ROOT / "src" / f"udb{MODULE_EXTENSION}", ROOT / "dist" / f"udb{MODULE_EXTENSION}")


def selected_module() -> Path:
    override = os.environ.get("UDB_MODULE_PATH")
    candidates = ([Path(override)] if override else []) + list(MODULE_CANDIDATES)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f"No compiled module; build src/udb{MODULE_EXTENSION} or set UDB_MODULE_PATH")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


class RuntimeModuleFreshnessTest(unittest.TestCase):
    def test_selected_module_matches_build(self):
        builds = [path for path in MODULE_CANDIDATES if path.is_file()]
        self.assertTrue(builds,
            f"No checkout build; build udb/src/udb with the current platform toolchain")
        selected = selected_module()

        # A direct selection must pass its own freshness check. External copies
        # can match either build, whose platform binaries need not be identical.
        matches = [path for path in builds if path.resolve() == selected]
        if not matches:
            selected_digest = sha256(selected)
            matches = [path for path in builds if sha256(path) == selected_digest]
        self.assertTrue(matches,
            "Selected module differs from every checkout build; set UDB_MODULE_PATH "
            "to a freshly compiled checkout module")

        newest_source = max(
            (path.stat().st_mtime for path in (ROOT / "src").rglob("*") if path.suffix in (".c", ".h", ".inc")),
            default=0.0,
        )
        stale = []
        for expected in matches:
            newest_input = newest_source
            if expected == MODULE_CANDIDATES[1]:
                bundle = ROOT / "dist" / "udb.c"
                if not bundle.is_file():
                    stale.append(f"{expected}: missing generated source {bundle}")
                    continue
                newest_input = max(newest_input, bundle.stat().st_mtime)
            if newest_input <= expected.stat().st_mtime:
                return
            stale.append(f"{expected}: older than source")
        self.fail("Stale build: " + "; ".join(stale) + ". Rebuild the checkout module")


if __name__ == "__main__":
    unittest.main()
