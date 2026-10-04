# Replacement test suite

[Español](README_ES.md)

The cmocka + pytest suite is the authoritative test suite. Its assertions are
derived from current UDB source, protocol and documented behavior; previous
test expectations and AST checkpoint counts are not behavioral requirements
or completion metrics. Standalone pre-pytest test scripts are not part of the
suite or CI entrypoints.

## Local setup

Requirements: Python 3.12+, CMake, a C compiler, OpenSSL/PCRE2 development
libraries and a configured UnrealIRCd 6.2.x source tree. Runtime cases also
require an installed UnrealIRCd test runtime and bubblewrap. Missing mandatory
capabilities fail rather than silently skip.

From the repository root:

```bash
python3 -m venv tests/.build/venv
. tests/.build/venv/bin/activate
python3 -m pip install -r tests/requirements.txt
python3 tests/support/bootstrap_cmocka.py
export UNREALIRCD_SOURCE=/path/to/configured/unrealircd-source
```

The explicit bootstrap downloads checksum-pinned cmocka 2.0.2 and installs it
only under ignored `tests/.build/`. Collection never installs dependencies.
`UNREALIRCD_SOURCE` overrides the configured source-tree location;
`CMOCKA_PREFIX` overrides the private cmocka installation.

## Run the smallest relevant family

```bash
python3 -m pytest -q -m tooling
python3 -m pytest -q -m unit
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_odr_violation=2 \
  python3 -m pytest -q -m unit --c-profile asan
```

Every canonical C case includes `src/udb.c`, runs in its own cmocka process and
uses strict adapters only for external daemon dependencies. UDB helpers are
not replaced by mocks. `--c-profile asan` instruments these C executables with
ASan/UBSan; it does not rebuild or instrument an installed daemon.

Before runtime checks, build the canonical module in the configured UnrealIRCd
source tree (where this checkout is `src/modules/third/udb`):

```bash
make -C "$UNREALIRCD_SOURCE" custommodule MODULEFILE=udb/src/udb
export UDB_MODULE_PATH="$PWD/src/udb.so"
# Set this if the installed test runtime is not $HOME/unrealircd:
export UDB_TEST_IRCD_ROOT=/path/to/installed/test-runtime
python3 -m pytest -q -m 'integration or protocol or recovery or model'
```

Each fixture copies the supplied module into disposable loopback nodes;
read-only mounts protect the installed runtime and owned process groups ensure
cleanup. The tests do not replace a user's installed module or restart existing
servers. A sanitizer runtime check requires a separately sanitizer-built daemon
and module, as supplied by the CI matrix.

## Full discovery and reports

```bash
python3 -m pytest -q --junitxml tests/.build/results.xml
```

CI runs current C unit/tooling and runtime/protocol/recovery/model gates in both
existing normal and sanitizer build profiles. Canonical C sanitizer cases enable leak and ODR detection. Runtime
cases retain the existing daemon sanitizer environment; this is not a claim
that daemon leak detection is enabled. The offline hash-index benchmark is
opt-in: run `python3 -m pytest -q -m perf tests/perf` or add `--run-perf` to a
broader run. It measures parsing and hash distribution only, not daemon runtime
or fsync performance.

New tests assert behavior and independently computed digests, not production C
source spelling. Malformed input, timeout, divergence or unavailable required
infrastructure must not become an `xfail`, retry or capability skip.
