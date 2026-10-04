"""Collect canonical C cases as first-class, separately isolated pytest items."""

import os
from pathlib import Path
import subprocess

import pytest

from udb_test_support.build import build_c_suite
from udb_test_support.cases import parse_case_list
from udb_test_support.node import UdbNode


def pytest_addoption(parser):
    parser.addoption("--c-profile", choices=("normal", "asan"), default="normal")
    parser.addoption("--run-perf", action="store_true", help="explicitly run benchmark cases")


def pytest_collect_file(file_path: Path, parent):
    if file_path.suffix == ".c" and file_path.name.startswith("test_") and file_path.parent.name == "unit":
        return CanonicalCFile.from_parent(parent, path=file_path)


class CanonicalCFile(pytest.File):
    def collect(self):
        profile = self.config.getoption("--c-profile")
        # Selection such as -m tooling must not require a C compiler or daemon headers.
        marker_expression = self.config.option.markexpr
        from _pytest.mark.expression import Expression
        if marker_expression and not Expression.compile(marker_expression).evaluate(lambda name, **kwargs: name == "unit"):
            return
        if hasattr(self.config, "_udb_c_build_error"):
            raise RuntimeError(self.config._udb_c_build_error)
        if not hasattr(self.config, "_udb_c_build"):
            try:
                self.config._udb_c_build = build_c_suite(profile)
            except Exception as error:
                self.config._udb_c_build_error = str(error)
                raise
        binary = self.config._udb_c_build / self.path.stem
        listing = subprocess.run([str(binary), "--list"], text=True, capture_output=True, timeout=30)
        if listing.returncode:
            raise RuntimeError(f"C case listing failed ({listing.returncode}):\n{listing.stdout}{listing.stderr}")
        for case in parse_case_list(listing.stdout):
            item = CanonicalCItem.from_parent(self, name=f"{case.suite}/{case.name}",
                                             binary=binary, case=case.name)
            item.add_marker("unit")
            yield item


class CanonicalCItem(pytest.Item):
    def __init__(self, *, binary, case, **kwargs):
        super().__init__(**kwargs)
        self.binary, self.case = binary, case

    def runtest(self):
        # A new process resets all canonical static/global state for each case.
        result = subprocess.run([str(self.binary), "--case", self.case],
                                capture_output=True, text=True, timeout=60, env=os.environ.copy())
        self.add_report_section("call", "C harness", result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(f"C case exited {result.returncode}:\n{result.stdout}{result.stderr}")

    def reportinfo(self):
        return self.path, 0, self.name

    def repr_failure(self, excinfo, style=None):
        return f"{self.nodeid}\n{excinfo.value}"


@pytest.fixture
def node_factory(tmp_path):
    nodes = []

    def create(*, ready=True, path=None, prepare=None, expected_state="READY", **options):
        node_path = Path(path) if path is not None else tmp_path / f"node-{len(nodes) + 1}"
        node = UdbNode(node_path, ready=ready, prepare=prepare, expected_state=expected_state, **options)
        nodes.append(node)
        return node

    yield create

    for node in reversed(nodes):
        node.close()


def pytest_collection_modifyitems(config, items):
    for item in items:
        if item.get_closest_marker("perf") and not config.getoption("--run-perf") and config.option.markexpr != "perf":
            item.add_marker(pytest.mark.skip(reason="benchmark is opt-in: -m perf or --run-perf"))
