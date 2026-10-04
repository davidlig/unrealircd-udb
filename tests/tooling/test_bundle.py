"""Exercise the real bundle generator on disposable input/output trees."""

import hashlib
import shutil
import subprocess
import sys

import pytest

from udb_test_support.modules import ROOT, repository_module

pytestmark = pytest.mark.tooling


@pytest.fixture
def bundle(tmp_path, monkeypatch):
    module = repository_module("scripts/bundle.py")
    source = tmp_path / "src"
    shutil.copytree(ROOT / "src", source, ignore=shutil.ignore_patterns("*.so", "*.o"))
    monkeypatch.setattr(module, "SRC_DIR", str(source))
    return module, source


def test_generation_is_reproducible_and_never_edits_inputs(bundle):
    module, source = bundle
    before = {p.name: p.read_bytes() for p in source.iterdir() if p.is_file()}
    first = module.build_bundle()
    second = module.build_bundle()
    assert first == second == (ROOT / "dist/udb.c").read_bytes()
    assert before == {p.name: p.read_bytes() for p in source.iterdir() if p.is_file()}
    assert hashlib.sha256(first).hexdigest().encode() in module.build_modules_list(first)
    assert module.build_modules_list(first) == (ROOT / "modules.list").read_bytes()


def test_cli_generation_and_check_reproduce_artifacts_without_source_mutation(bundle):
    _, source = bundle
    root = source.parent
    script = root / "scripts/bundle.py"
    script.parent.mkdir()
    shutil.copy2(ROOT / "scripts/bundle.py", script)
    before = {p.name: p.read_bytes() for p in source.iterdir() if p.is_file()}
    snapshots = []
    for args in ([], [], ["--check"]):
        result = subprocess.run([sys.executable, str(script), *args], cwd=root,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        snapshots.append(tuple((root / path).read_bytes() for path in ("dist/udb.c", "modules.list")))
    assert snapshots[0] == snapshots[1] == snapshots[2]
    assert snapshots[0] == tuple((ROOT / path).read_bytes() for path in ("dist/udb.c", "modules.list"))
    assert before == {p.name: p.read_bytes() for p in source.iterdir() if p.is_file()}


def test_read_only_check_verifies_current_artifacts_without_writing():
    paths = [ROOT / "dist/udb.c", ROOT / "modules.list"]
    before = [(p.stat().st_mtime_ns, p.read_bytes()) for p in paths]
    result = subprocess.run([sys.executable, str(ROOT / "scripts/bundle.py"), "--check"],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert before == [(p.stat().st_mtime_ns, p.read_bytes()) for p in paths]


@pytest.mark.parametrize("fault", ["orphan", "missing", "duplicate", "cycle"])
def test_invalid_composition_cannot_generate_a_bundle(bundle, fault):
    module, source = bundle
    if fault == "orphan":
        (source / "orphan.c.inc").write_text("int orphan;\n")
    elif fault == "missing":
        (source / "udb_store.c.inc").unlink()
    elif fault == "duplicate":
        with (source / "udb.c").open("a") as handle:
            handle.write('#include "udb_store.c.inc"\n')
    else:
        with (source / "udb_store.c.inc").open("a") as handle:
            handle.write('#include "udb_store.c.inc"\n')
    with pytest.raises(SystemExit):
        module.build_bundle()


def test_atomic_publication_replaces_bytes_and_leaves_no_temp(bundle, tmp_path):
    module, _ = bundle
    target = tmp_path / "publication"
    target.write_bytes(b"old")
    module.atomic_write(str(target), b"new")
    assert target.read_bytes() == b"new"
    assert not (tmp_path / "publication.tmp").exists()
