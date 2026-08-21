# Isolated Two-Node UDB Harness

Run from any directory:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py
```

The harness creates two temporary configs, two independent UDB data trees, and
four loopback ports. It loads the supplied compiled UDB module on both nodes.
`bwrap` gives each node a distinct mount at UnrealIRCd's compiled data path,
which is required because UDB block files currently use `PERMDATADIR` despite
the `udb::database-directory` setting. The host filesystem is read-only inside
each node except for its temporary directory, data mount, and module-cache
mount (`/home/davidlig/unrealircd/tmp`).

It compiles `udb_test_mutator.c` with the normal `make custommodule` target,
bind-mounts it only into node A, and configtests both generated configurations.
The fixture runs only after A sees B's post-EOS sync hook and after the harness
creates its test-only arm file following staged-sync settlement. It then waits
three seconds, emits an `INS` as the configured propagator, holds it for three
seconds, and emits its matching `DEL`.

The harness seeds different `N` blocks (with
node B deliberately older), starts node B and then node A, and waits for both
logs to report a linked and synced S2S connection. It then requires the staged
`HEL`, `INF`, `RES`, `BEGIN`, `PUT`, `END`, and `ACK` exchange and the `N` block from A
to be committed in B. A `PASS` therefore means real module loading, link
establishment, negotiated UDB capability, and staged synchronization occurred.
It additionally requires node B to receive the fixture `INS` and `DEL`, persist
the inserted record before deletion, and persist its absence after deletion.
The fixture is test-only and does not alter the UDB production module.

To cover the persistence-failure path deterministically, run:

```sh
python3 src/modules/third/udb/tests/two_node_udb.py --snapshot-rename-failure
```

This additionally builds a test-only `LD_PRELOAD` fixture and applies it only to
node B. It returns `EIO` only when the configured `udb_N.db.tmp` is renamed to
`udb_N.db`. The harness then requires B's original database bytes to remain
unchanged, no `.tmp` file, no staged `ACK`/commit, and both the interposer and
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
- An installed UnrealIRCd 6 runtime at `/home/davidlig/unrealircd`, including
  `bin/unrealircd` and `conf/modules.default.conf`. Override the binary with
  `--ircd`; the installed module configuration currently remains required.
- A compiled UDB module at `src/modules/third/udb/src/udb.so`, or pass
  `--module /absolute/path/to/udb.so`.
- Loopback TCP connections and six available ephemeral ports.

If bubblewrap is prohibited by the host, do not run two direct instances: their
compiled `PERMDATADIR` would overlap and invalidate the staged-sync assertion.
