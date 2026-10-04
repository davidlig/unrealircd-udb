"""Load repository tools, never legacy test entrypoints."""

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def repository_module(relative_path):
    path = ROOT / relative_path
    specification = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module
