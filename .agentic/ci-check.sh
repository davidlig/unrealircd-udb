#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

python3 -m pytest -q -m tooling tests/tooling/test_agentic.py tests/tooling/test_ci.py
python3 .agentic/generate.py --check
