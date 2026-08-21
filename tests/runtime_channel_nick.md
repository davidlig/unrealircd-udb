# One-Node Nick and Channel Runtime Smoke Test

Run against a prepared, isolated UnrealIRCd instance:

```sh
python3 src/modules/third/udb/tests/runtime_channel_nick.py
```

Set `UDB_TEST_HOST` and `UDB_TEST_PORT` when the instance is not listening on
`127.0.0.1:16667`.

This is a client-side fixture, not a server launcher. Before running it, preload
the server with `N::alice` and `C::#vault` records using only an Argon2id,
SHA-256, or crypt password and matching `challenge` values. The fixture verifies
nick `+r` and vhost application/removal, founder-only `+q`, password `JOIN`
granting `+a`, and a password-bearing local `INVITE` granting one entry without
`+a`.

Use the two-node harness for HEL 4, staged synchronization, and the authorized
real-time `INS`/`DEL` mutator fixture. Use the three-node harness to prove that
the A-to-B commit occurs before B synchronizes the record to C.
