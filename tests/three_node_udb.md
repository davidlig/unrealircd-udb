# Isolated Three-Node UDB Harness

Run from any directory:

```sh
python3 src/modules/third/udb/tests/three_node_udb.py
```

The harness creates an A-B-C topology with nine loopback ports and a separate
Bubblewrap mount namespace, temporary directory, and configured UDB database
directory for each node. UDB is loaded on every node. Only A contains the
`N`-block marker; B and C begin with empty, deliberately old block placeholders.
The separate runtime-data mount is used only for UnrealIRCd's control socket;
UDB uses each configured database directory instead.

It compiles the existing test-only `udb_test_mutator.c` fixture and loads it
on A, B, and C from their isolated module trees. The generated configurations
use edge-local propagators: B authorizes A for A-to-B mutations and C authorizes
B for B-to-C mutations.

It starts B and A, requires A-B link/sync evidence and load-log evidence that
both read their seeded blocks from their configured temporary directories, and
requires the complete received staged sequence `HEL`, `INF`, `BEGIN`, `PUT`,
`END` plus A's marker in B's isolated data tree. Only then does it start C. The new B-C link must show
link/sync evidence and the same staged-frame sequence in C before C's isolated
tree contains the marker. This proves the record committed in B before B
propagated it to C, rather than merely proving a three-node network converged.
It then arms C's test-only fixture to send `BEGIN`, `PUT`, `END`, and `RES` to
B. C has completed HEL but B selects A as its propagator, so B must reject all
four frames, leave its database byte-identical, and send no staged export to C.
Once the staged A-to-B-to-C path is complete, the harness arms the test-only
fixture and requires an authorized `INS` and matching `DEL` on both direct
edges: B must durably contain and then omit A's record, and C must durably
contain and then omit B's edge-specific record. The fixture is test-only and
does not alter the production UDB module.

Every timeout prints relevant `Server linked`, `is now synced`, `[UDB]`, and
fixture log lines, along with the received UDB command sequence and affected
receiver database. `SKIP` exits with status 77 and is never a passing synchronization: it
means Bubblewrap, the installed runtime/module configuration, or the required
S2S environment was unavailable. Bubblewrap namespace permission failures are
also reported as `SKIP`.

Use `--timeout SECONDS` to change each link/staged-sync wait (default 15). Use
`--keep` to retain the generated configs, data trees, and logs; the retained
temporary path is printed at exit. Without it, the harness removes its temporary
directory after stopping all three processes.

## Prerequisites

- A non-root account that can run `bwrap`; user namespaces must be enabled.
- An installed UnrealIRCd runtime at `$HOME/unrealircd`, including
  `bin/unrealircd` and `conf/modules.default.conf`. Use `--ircd` to override the
  binary or set `UDB_TEST_IRCD_ROOT` to select a different runtime root.
- A compiled module at `src/modules/third/udb/src/udb.so`, or pass
  `--module /absolute/path/to/udb.so`.
- Loopback TCP connectivity and nine available ephemeral ports.

The generated configurations isolate UDB through distinct absolute
`database-directory` paths. Bubblewrap remains required by this harness for its
read-only module/runtime mount layout.
