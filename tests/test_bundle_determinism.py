#!/usr/bin/env python3
"""Determinism and hygiene contract for the UDB bundle generator."""

import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
BUNDLE = REPO_ROOT / "scripts" / "bundle.py"
DIST_C = REPO_ROOT / "dist" / "udb.c"
MODULES_LIST = REPO_ROOT / "modules.list"


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    failures = []

    # 1. Committed artifacts must match the canonical sources (read-only check).
    result = run([sys.executable, str(BUNDLE), "--check"], cwd=REPO_ROOT)
    if result.returncode != 0:
        failures.append(f"--check failed against the repository:\n{result.stdout}")

    # 2. Generating in a pristine copy must reproduce the committed bytes and
    #    must never mutate the canonical sources.
    with tempfile.TemporaryDirectory(prefix="udb-bundle-determinism-") as tmp:
        root = pathlib.Path(tmp)
        shutil.copytree(REPO_ROOT / "src", root / "src")
        scripts = root / "scripts"
        scripts.mkdir()
        shutil.copy(BUNDLE, scripts / "bundle.py")
        before = {p.relative_to(root): p.read_bytes() for p in sorted((root / "src").rglob("*")) if p.is_file()}

        first = run([sys.executable, str(scripts / "bundle.py")], cwd=root)
        if first.returncode != 0:
            failures.append(f"generation failed:\n{first.stdout}")
        second = run([sys.executable, str(scripts / "bundle.py")], cwd=root)
        if second.returncode != 0:
            failures.append(f"second generation failed:\n{second.stdout}")

        generated_c = root / "dist" / "udb.c"
        generated_list = root / "modules.list"
        if not generated_c.is_file() or not generated_list.is_file():
            failures.append("generation did not produce dist/udb.c and modules.list")
        else:
            if generated_c.read_bytes() != DIST_C.read_bytes():
                failures.append("generated dist/udb.c differs from the committed artifact")
            listed = hashlib.sha256(generated_c.read_bytes()).hexdigest()
            if listed not in generated_list.read_text():
                failures.append("modules.list sha256sum does not match the generated dist/udb.c")

        check = run([sys.executable, str(scripts / "bundle.py"), "--check"], cwd=root)
        if check.returncode != 0:
            failures.append(f"--check failed inside the generated copy:\n{check.stdout}")

        after = {p.relative_to(root): p.read_bytes() for p in sorted((root / "src").rglob("*")) if p.is_file()}
        if before != after:
            failures.append("the generator mutated canonical sources")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: bundle is reproducible, self-consistent, and read-only over src/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
