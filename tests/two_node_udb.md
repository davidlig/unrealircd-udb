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

It configtests both generated configurations, seeds different `N` blocks,
starts node B and then node A, and requires both evidence of UDB DB traffic and
the staged `N` block from A to be committed in B. A `PASS` therefore means real
module loading, negotiated UDB capability, and staged synchronization occurred.

`SKIP` (exit status 77) is deliberate, never a successful synchronization. It
means the local installation could not provide the isolation or S2S conditions.
Use `--keep` to retain the generated configs and logs for diagnosis.

## Prerequisites

- A non-root account that can run `bwrap`; user namespaces must be enabled.
- An installed UnrealIRCd 6 runtime at `/home/davidlig/unrealircd`, including
  `bin/unrealircd` and `conf/modules.default.conf`. Override the binary with
  `--ircd`; the installed module configuration currently remains required.
- A compiled UDB module at `src/modules/third/udb/src/udb.so`, or pass
  `--module /absolute/path/to/udb.so`.
- Loopback TCP connections and four available ephemeral ports.

If bubblewrap is prohibited by the host, do not run two direct instances: their
compiled `PERMDATADIR` would overlap and invalidate the staged-sync assertion.
