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
that daemon leak detection is enabled. The native storage benchmark is opt-in; see the command below. It does not
measure daemon throughput, power loss or cold-device I/O.

New tests assert behavior and independently computed digests, not production C
source spelling. Malformed input, timeout, divergence or unavailable required
infrastructure must not become an `xfail`, retry or capability skip.


## Exclusive native persistence qualification

Only current contracts are discovered. Obsolete tree, text snapshot and old wire
fixtures have been removed, not rerun as compatibility checks.

```bash
python3 -m pytest -q tests/native/test_storage.py
python3 -m pytest -q tests/native/test_storage.py --c-profile asan
UDB_MODULE_PATH="$PWD/src/udb.so" python3 -m pytest -q tests/protocol/test_native_persistence.py
python3 tests/perf/benchmark_native.py --rows 1000000 --cache-mib 128 --timeout 120 --output tests/.build/bench-results/1m-128.json
```

The native driver includes the production storage units and executes real file
I/O, WAL and writer jobs. Its structural validators deliberately do not claim
UnrealIRCd business-schema coverage. The controlled real-daemon peer computes
binary/radix digests independently and uses explicit typed values and origin-bearing
INF. A socket PING is not a writer commit barrier; cases observe durable results.

Benchmark rows are unique nick vhost properties. Bulk sorting/rebuild runs the
production core on an owner-serialized stopped writer; it is not network import or
per-mutation durability throughput. Recovery performs the actual full reference
validation. First-pass reads begin with DATA cache cold but warmed indices and an
OS page cache retained from creation; repeat reads reuse the same keys. Reports
contain measured cache/AUX peaks, process RSS and stable-file sizes, not projections.
Owned scratch stores are removed after measurement, never installed daemon data.

Linux tests cannot establish MSVC v142 compilation, Windows 11/Server 2022/2025
NTFS crash behavior/ACLs, actual power-loss durability, or untested runtime
ownership/topology cases. Keep those boundaries explicit until observed.

Recorded bulk/read matrix and a 10M-row durable point probe are in
[`doc/native_storage_benchmarks.json`](../doc/native_storage_benchmarks.json).
The new three-node case exercises production snapshot senders/receivers and
root-origin live mutation relay, not just controlled-peer imports. The driver
injects EIO/ENOSPC around real POSIX I/O and exits before WAL, after half a WAL
page or after its flush; these do not simulate lost device caches or power loss.
