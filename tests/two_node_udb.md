# Isolated Two-Node UDB Harness

Run from any directory:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py
```

The harness creates two temporary configs, two independent UDB data trees, and
four loopback ports. Each config sets `udb::database-directory` to its node's
temporary data tree. It loads the supplied compiled UDB module on both nodes.
`bwrap` keeps the host root read-only while each node directory, including its
configured database directory, remains writable. Its separate runtime-data
mount is only for UnrealIRCd's control socket; UDB does not use that mount. The
module-cache mount (`$HOME/unrealircd/tmp`) is also writable.

It compiles `udb_test_mutator.c` with the normal `make custommodule` target,
bind-mounts it only into node A, and configtests both generated configurations.
The fixture runs only after A sees B's post-EOS sync hook and after the harness
creates its test-only arm file following staged-sync settlement. It then waits
three seconds, emits an `INS` as the configured propagator, holds it for three
seconds, and emits its matching `DEL`.

The harness seeds divergent `N` and `K` blocks with exactly equal mtimes, starts
node B and then node A, and waits for both logs to report a linked and synced
S2S connection. B's immutable SID (`0B1`) sorts above A's (`0A1`), so it is the
defined winner. The harness requires one `RES` per divergent block, followed by `BEGIN`, `PUT`,
`END`, and `ACK`, and verifies B's `N` block plus nested `K` line commit in A.
A `PASS` therefore proves real module loading, negotiated UDB capability,
deterministic equal-timestamp resolution, and no reciprocal snapshot exchange.
It additionally requires both nodes to log loading their seeded N/K blocks from
their configured temporary database directories. Node B must receive the fixture
`INS` and `DEL`, persist the inserted record before deletion, and persist its
absence after deletion. The normal mode then restarts B and requires a fresh
load-log entry for B's persisted N block in that same configured directory.
The fixture is test-only and does not alter the UDB production module.

To cover the persistence-failure path deterministically, run:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py --snapshot-rename-failure
```

This additionally builds a test-only `LD_PRELOAD` fixture and applies it only to
node A. It returns `EIO` only when the configured `udb_N.db.tmp` is renamed to
`udb_N.db`. The harness then requires A's original database bytes to remain
unchanged, no `.tmp` file, no staged N-block `ACK`/commit, and both the interposer and
staged persistence-failure evidence in the logs. The normal invocation does not
build or preload this fixture and retains its existing successful-sync checks.

To exercise the equivalent live-mutation rollback after staged synchronization
has succeeded, run:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py --runtime-rename-failure
```

The interposer is armed only after B has committed A's initial snapshot. B then
receives the test mutator's authorized `INS`, fails its candidate snapshot
rename, returns `ERR`, and must retain byte-identical durable and active data.

To cover the transactional `OPT` failure path, run:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py --runtime-opt-rename-failure
```

After staged synchronization, the armed interposer fails B's snapshot rename for
an authorized `OPT`. The harness requires B's database bytes to remain unchanged,
no temporary file, interposer evidence, and `ERR OPT 6` returned to A.

`SKIP` (exit status 77) is deliberate, never a successful synchronization. It
means the local installation could not provide the isolation or S2S conditions.
Use `--keep` to retain the generated configs, data trees, and logs for
diagnosis. The retained path is printed at exit and is not cleaned up.

When an S2S link is confirmed but a snapshot or fixture mutation is missing, the
harness prints the received UDB command sequence for each node, fixture and UDB
log lines (including protocol errors), and node B's database contents. In
particular, no received `HEL` after both links sync points to the current
`HOOKTYPE_SERVER_SYNC` path not starting capability negotiation. An observed
`HEL` without `INF` means its explicit acknowledgement was not accepted; this is
diagnostic evidence, not a pass.

## Prerequisites

- A non-root account that can run `bwrap`; user namespaces must be enabled.
- An installed UnrealIRCd 6 runtime at `$HOME/unrealircd`, including
  `bin/unrealircd` and `conf/modules.default.conf`. Override the binary with
  `--ircd`. Set `UDB_TEST_IRCD_ROOT` to use a different runtime root; the
  installed module configuration currently remains required.
- A compiled UDB module at `src/modules/third/udb/src/udb.so`, or pass
  `--module /absolute/path/to/udb.so`.
- Loopback TCP connections and six available ephemeral ports.

If bubblewrap is prohibited by the host, the harness skips because it uses its
read-only module/runtime mount layout. The generated configurations themselves
keep UDB data isolated through distinct absolute `database-directory` paths.
