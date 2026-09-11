#!/bin/sh
# Explicit, standalone formatting step for canonical UDB sources.
# The bundle generator (scripts/bundle.py) never formats anything; run this
# before regenerating the bundle so dist always comes from formatted sources.
set -e
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)/src"
clang-format -i "$SRC_DIR"/*.c "$SRC_DIR"/*.h "$SRC_DIR"/*.c.inc
echo "[+] Formatted canonical sources in $SRC_DIR"
echo "[+] Now run: python3 scripts/bundle.py"
