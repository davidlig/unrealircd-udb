# UDB 4 for UnrealIRCd 6

UDB (Unreal DataBase) is a global **UnrealIRCd 6** module that maintains a distributed database for nicknames, channels, per-IP policy, settings, per-server options, and sanctions, with atomic persistence, staged snapshot reconciliation, and explicit authority control.

**Current module version:** `4.0.0`  
**Declared compatibility:** UnrealIRCd `6.2.*`  
**Module name:** `third/udb`  
**License:** GPL v2 or later

This documentation was rebuilt from `main` code at commit `75d017117d934f9dcb64dbeabe99d1888b72dcab` (2026-09-10).

## What UDB manages

| Block | Content |
|---|---|
| `N` | Nicks: password, CIDR access, vhost, operclass, modes, snomasks, SWHOIS, forbid/suspended |
| `C` | Channels: founder, modes, topic, access, password, forbid/suspended, options |
| `I` | IP/realhost: clone limits, `nolines` exemptions, explicit host/vhost |
| `S` | Global settings: clones, flood, service masks, vhost key/suffix, propagator |
| `L` | Per-server options, currently `DEBUG` |
| `K` | G-Line, Z-Line, Shun, Q-Line, Spamfilter |

UDB is **not an interactive NickServ/ChanServ registration service**. A selected authority/Services normally changes the database through the S2S `DB` protocol; each IRCd validates, persists, and applies those changes.

## Highlights

- Six persistent blocks with strict schema validation.
- Copy-on-write mutations: persistence occurs before new runtime state is published.
- Atomic snapshots using a temporary file, `fsync`, `rename`, and parent-directory `fsync`.
- Durable `.udb_state`: READY is valid only when all six blocks belong to one generation.
- Mandatory **HEL 4** negotiation and mandatory **OCL** capability between UDB peers.
- `INF → RES → BEGIN/PUT/END → ACK` reconciliation with checksums, staging caps, inactivity and absolute timeouts.
- Propagator/authority selection only from directly linked peers.
- Ordered failover through distributed `S::propagator`.
- Separate `READY/BOOTSTRAPPING` readiness and `OK/DEGRADED/STALE` sync health.
- Fail-closed admission: a NOT_READY node rejects new local clients until a durable database is recovered.
- Distributed operclass registry **OCL** and globally consistent **OCLG** projection.
- Multihop live mutations while staged snapshot transfers stay hop-by-hop.

## Installation

The repository ships a standalone bundle at:

```text
dist/udb.c
```

`modules.list` registers `third/udb` for UnrealIRCd `6.2.*`. After installing/compiling the module using UnrealIRCd's module mechanism, load it with:

```text
loadmodule "third/udb";
```

Typical minimal configuration when this node should obtain its database from Services/another peer:

```text
udb {
    propagator "ares-services.example.net";
};
```

Validate configuration and restart:

```bash
./unrealircd configtest
./unrealircd restart
```

With no propagator policy, a truly fresh database directory initializes as a **standalone authority** with an empty READY database. Once READY and still policy-free, that node accepts no remote imports.

## Configuration

```text
udb {
    database-directory "/local/path";
    propagator "ares-services.example.net";
    max-global-clones 0;
    password-flood "5:60";
    max-staged-records 500000;
    max-staged-bytes 67108864;
    sync-inactivity-timeout 60;
    sync-absolute-timeout 300;
    stale-timeout 300;
};
```

| Directive | Default | Note |
|---|---:|---|
| `database-directory` | `PERMDATADIR` | Holds `udb_[NCISLK].db` and `.udb_state` |
| `propagator` | — | One server name in local configuration |
| `max-global-clones` | 0 | **Currently parsed but not consumed at runtime** |
| `password-flood` | `5:60` | Failed attempts per profile/IP and time window |
| `max-staged-records` | 500000 | Incoming snapshot cap |
| `max-staged-bytes` | 64 MiB | Accumulated staged payload cap |
| `sync-inactivity-timeout` | 60 s | Refreshed by PUT activity |
| `sync-absolute-timeout` | 300 s | Never refreshed by activity |
| `stale-timeout` | 300 s | NOT_READY age before STALE |

Unlike local `udb::propagator`, distributed `S::propagator` may contain an ordered list:

```text
udb-a.example.net,udb-b.example.net,udb-c.example.net
```

Local configuration takes precedence over S.

## Persistence

The configured database directory contains:

```text
udb_N.db
udb_C.db
udb_I.db
udb_S.db
udb_L.db
udb_K.db
.udb_state
```

All six snapshots form one READY generation set. On startup UDB refuses automatic READY if a block is missing/corrupt, generations differ, `.udb_state` is invalid, or `.udb_previous` backups indicate an unfinished transaction.

Do not edit these files while the daemon is running. Authorized protocol mutations preserve schema, checksums, atomicity, and runtime effects.

## Protocol and synchronization

A direct link negotiates:

```text
DB <peer> HEL 4 <selector> <epoch> OCL [OCLG]
```

The rest of the protocol is not accepted until HEL 4/OCL is confirmed; HEL timeout can cause UDB to abort the server link.

Reconciliation compares all six blocks with `INF`. Only divergent blocks are requested through `RES` and received into a private staged tree using `BEGIN/PUT/END`. END validates the checksum, persists the snapshot, and only then publishes the new tree.

Live mutations are `INS`, `DEL`, `DRP`, and `OPT`. They can be relayed multihop; staged snapshot transfers are never forwarded.

See [doc/udb_technical_en.md](doc/udb_technical_en.md) for the complete grammar and authority invariants.

## User-facing behavior

For a registered nick:

```text
/NICK alice:Password
```

To recover an occupied registered nick:

```text
/NICK alice!Password
/GHOST alice Password
```

When `N::access` exists, the client IP must also match an authorized CIDR.

For a registered channel with `pass`, use the password as JOIN key:

```text
/JOIN #channel Password
```

Successful authentication may grant `+a`; an identified founder receives `+q`. The extension:

```text
/INVITE nick #channel Password
```

validates the channel password and creates a temporary invite grant for a local target.

## Operator diagnostics

```text
/UDB STATUS
/UDB OPERCLASSES [filter]
/UDB OPERCLASS <name>
/DBQ <block>[::path]
/DBQ <server> <block>[::path]
```

Start with `/UDB STATUS`: it reports readiness, sync health, recovery state, selected propagator, policy, availability, and local-client admission.

`/UDB OPERCLASSES` shows whether all participating IRCds have supplied OCL inventories. `/UDB OPERCLASS <name>` verifies that a class exists with the same effective digest everywhere.

### Current DBQ warning

The implementation redacts `N::*::pass`, `N::*::challenge`, and `S::encryption_key`, but **does not currently redact `C::*::pass` or `C::*::challenge`**. Until fixed, treat DBQ as a privileged interface that may reveal channel credentials to authorized opers.

## Development

Canonical sources live in `src/`; do not edit `dist/udb.c` as the primary source.

Regenerate the bundle:

```bash
python3 scripts/bundle.py
```

Check for bundle drift without writing:

```bash
python3 scripts/bundle.py --check
```

Current CI builds UnrealIRCd 6.2.6, validates the deterministic bundle, and runs normal plus ASan/UBSan suites covering persistence, bootstrap, staged sync, multihop, failover, OCL, limits, and runtime effects.

## Documentation

- [Technical documentation in English](doc/udb_technical_en.md)
- [Documentación técnica en español](doc/udb_technical_es.md)
- [README en español](README_ES.md)
