# UDB 4 for UnrealIRCd 6

UDB (Unreal DataBase) is a global **UnrealIRCd 6** module that maintains a distributed database for nicknames, channels, per-IP policy, settings, per-server options, and sanctions, with atomic persistence, staged snapshot reconciliation, and explicit authority control.

**Current module version:** `4.0.0`
**Declared compatibility:** UnrealIRCd `6.2.*`
**Module name:** `third/udb`
**License:** GPL v2 or later

## What UDB manages

| Block | Content |
|---|---|
| `N` | Nicks: password, CIDR access, vhost, operclass, modes, snomasks, SWHOIS, forbid/suspend |
| `C` | Channels: founder, modes, topic, access, forbid/suspend, options |
| `I` | IP/realhost: clone limits, `nolines` exemptions, explicit host/vhost |
| `S` | Global settings: clones, flood, service masks, vhost key/suffix, propagator |
| `L` | Per-server options, currently `DEBUG` |
| `K` | G-Line, Z-Line, Shun, Q-Line, Spamfilter |

UDB is **not an interactive NickServ/ChanServ registration service**. A selected authority/Services normally changes the database through the S2S `DB` protocol; each IRCd validates, persists, and applies those changes. Block K uses one canonical profile schema; its exact G/Z/S/Q and dynamic Spamfilter F contract is documented in the technical guide.

## Highlights

- Six persistent blocks with strict schema validation.
- Copy-on-write mutations: persistence occurs before new runtime state is published.
- Atomic snapshots using a temporary file, `fsync`, `rename`, and parent-directory `fsync`.
- Durable `.udb_state`: READY is valid only when all six blocks belong to one generation.
- Mandatory **HEL 4** negotiation and mandatory **OCL** capability between UDB peers.
- `INF → RES → BEGIN/PUT/END → ACK` reconciliation with canonical SHA-256 state manifests, staging caps, inactivity and absolute timeouts.
- Authoritative freshness (Policy A): selected propagator is authoritative; local filesystem timestamps (`mtime`) never override network state or decide record ownership.
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
    propagator "ares-services.example.net";
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
| `propagator` | — | One server name in local configuration |
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

UnrealIRCd `PERMDATADIR` contains:

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

The rest of the protocol is not accepted until HEL 4/OCL is confirmed; HEL timeout can cause UDB to abort the server link. HEL is capability/authority negotiation, not cryptographic peer authentication: UDB inherits the UnrealIRCd server-link trust boundary. Use authenticated TLS links with certificate verification on every UDB hop; without TLS, snapshots, password hashes, channel keys, and mutations are exposed in transit.

Block `K` uses `expires *<unix_timestamp>` for temporary G/Z/S/Q/F sanctions; no `expires` means permanent. Followers relay directed `EXP` requests hop by hop toward the selected root authority and never delete authoritative persistence locally; only that authority removes the expired subtree and originates the sequenced `DEL`.


### Block K IPv4/IPv6 routes

In a UDB path, `::` is the component separator, so every IPv6 `:` is `%3A`; printable `@` and `/` remain literal. For example, use `K::Z::2001%3Adb8%3A%3A1::reason`, `K::Z::2001%3Adb8%3A1234%3A%3A/48::reason`, `K::G::*@2001%3Adb8%3A%3A1::reason`, and `K::S::*@2001%3Adb8%3A%3A1::reason`. Z requires the canonical `inet_ntop()` address spelling and CIDR network address (no host bits); G/S retain `user@host` and do not yet canonicalize IPv6 hosts. See the technical guide for `DB INS`/`DEL` examples and the full policy.


Reconciliation compares all six blocks with `INF`. Only divergent blocks are requested through `RES` and received into a private staged tree using `BEGIN/PUT/END`. END validates the checksum, persists the snapshot, and only then publishes the new tree.

Live mutations are `INS`, `DEL`, `DRP`, and `OPT`; `EXP` is a directed follower-to-authority expiry request. Mutations retain the root origin SID/epoch/sequence while relays validate and forward them; staged snapshot transfers remain hop by hop and are never forwarded.

See [doc/udb_technical_en.md](doc/udb_technical_en.md) for the complete grammar and authority invariants.

## User-facing behavior

Only a profile containing `N::pass` is password-protected and can establish UDB identity:

```text
/NICK alice:Password
```

To recover an occupied password-protected nick:

```text
/NICK alice!Password
/GHOST alice Password
```

Without `N::pass`, a profile is not an identity: its nick may be used when `forbid` and `access` allow it, but it grants neither `account=<nick>` nor `+r`, vhost, oper, modes, snomasks, or SWHOIS. Those configured effects are dormant: UDB neither applies them nor treats them as negative policy that removes equivalent state supplied by another source, including when a dormant record is deleted or a full N snapshot replaces or removes the profile. `N::access` restricts nick use (and authentication when `pass` exists); matching it is never authentication. Password/recovery syntax has no ownership meaning for a profile without `pass`.

`N::snomasks` accepts relative expressions (`+...`, `-...`, or mixed `+c-k`); external snomasks are preserved when revoking. `N::oper` strictly respects external opers: UDB never replaces or claims an existing oper session, and revocation only de-opers sessions genuinely owned by UDB without polluting oper counters or wiping snomasks.

A `/NICK nick:Password` credential is one-shot and only applies to that attempt: the account, `+r`, and profile effects materialize only once the nick change is effective, and a rejected change (`433`, Q-line, nick-change limits) leaves the current nick's active UDB identity untouched. Initial registration follows the same rule, and a credential from a failed attempt is never reused by a later forced rename. When a formerly passless profile gains its first `N::pass`, `account`/`+r` from another source are not accepted as its authentication. A service-forced nick change (`SVSNICK`) is never UDB authentication: a protected nick reached without a valid active identity is safely renamed away.

A suspended password-protected nick still requires its normal `pass`/`access` checks before it can be adopted. `N::pass` supports the `argon2id`, `sha256`, and `crypt` types, selected by its own prefix (`argon2id:`, `sha256:`, or `crypt:`). Use `argon2id` for new credentials and migrate legacy `sha256`/`crypt` records when users authenticate; compatibility support does not make unsalted SHA-256 suitable for new passwords. Require TLS for client sessions that send `/NICK nick:Password` or `/GHOST`, because those commands carry the supplied password. Successful authentication is bound to the active nick only: while `N::suspend` exists UDB exposes no account/`+r`, applies no profile effects, and retains no authentication whatsoever. Removing `suspend` from a profile with `pass` renames the current holder, who must authenticate again with `/NICK nick:Password`; a profile without `pass` has no identity to revoke, so removing its `suspend` never identifies its holder. Account/`+r` alone cannot create identity. UDB neither adds nor removes externally owned `+S`.

A channel key is exclusively the native `+k` parameter in `C::<channel>::modes`, for example `+ntk secret`. It protects the first JOIN as well as later joins. An identified founder receives UDB-owned `+q`.

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

### Secret redaction

The implementation redacts `N::*::pass`, `S::encryption_key`, and the complete `C::<channel>::modes` value because it may contain a native `+k` key. Mode debug notices also redact parameters whenever the mode expression contains `k`.

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
