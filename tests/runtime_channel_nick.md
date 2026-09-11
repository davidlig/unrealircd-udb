# One-Node Nick and Channel Runtime Smoke Test

Run from the UnrealIRCd source root after building `udb.so`:

```sh
python3 src/modules/third/udb/tests/runtime_channel_nick.py
```

The harness creates a temporary one-node configuration and UDB data directory,
runs configtest, then starts the installed daemon inside a `bwrap` mount
namespace. It uses `UDB_TEST_IRCD_ROOT` (default: `~/unrealircd`) for the
installed daemon, default configuration includes, and runtime paths. Use
`--ircd`, `--module`, `--timeout`, or `--keep` when needed.

It seeds valid nickname records and channel modes. The protocol client verifies
nick password registration and vhost cleanup, founder-only `+q`, and native keyed
JOIN behavior. Every command waits for its IRC numeric
or protocol terminator rather than using timed input drains.

Use the two-node harness for HEL 4, staged synchronization, and the authorized
real-time `INS`/`DEL` mutator fixture. Use the three-node harness to prove that
the A-to-B commit occurs before B synchronizes the record to C.
