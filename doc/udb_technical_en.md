# UDB 4 — Technical documentation

> This document was rebuilt from the current implementation of `davidlig/unrealircd-udb`, `main` branch, commit `75d017117d934f9dcb64dbeabe99d1888b72dcab`, reviewed on September 10, 2026. The code, not previous versions of this documentation, was used as the source of truth.

## 1. Scope

UDB (Unreal DataBase) is a global UnrealIRCd 6 module that maintains and applies a distributed database for nickname registrations, channels, per-IP policy, settings, per-server options, and network sanctions. The module version is **4.0.0** and the distribution metadata declares **UnrealIRCd 6.2.x** as its minimum version (`min-unrealircd-version "6.2.*"`).

Canonical implementation sources live under `src/` and are deterministically amalgamated into `dist/udb.c`. The module is registered as `third/udb` and uses the S2S `DB` command for its protocol.

UDB does not implement an autonomous IRC registration service that creates accounts or channels. Authorized writes arrive through the `DB` protocol from the selected authority; each IRCd validates, persists, applies, and where appropriate relays those mutations.

## 2. Architecture

`src/udb.c` builds one compilation unit in this order:

1. `udb_store.c.inc`: record tree, paths, persistence.
2. `udb_config.c.inc`: `udb {}`, S settings, L options.
3. `udb_core.c.inc`: validation, schemas, checksums, tree operations.
4. `udb_services.c.inc`: NickServ/ChanServ/IpServ source resolution.
5. `udb_effects.c.inc`: runtime effect application/removal.
6. `udb_sync.c.inc`: HEL, authority, reconciliation, staged snapshots.
7. `udb_operclasses.c.inc`: OCL inventories and OCLG global view.
8. `udb_mutation.c.inc`: `INS`, `DEL`, `DRP`, `OPT`.
9. `udb_protocol.c.inc`: `DB` parsing and routing.
10. `udb_nicks.c.inc`, `udb_channels.c.inc`, `udb_ips.c.inc`, `udb_lines.c.inc`: block-specific runtime behavior.
11. `udb_query.c.inc`: `DBQ` and `UDB` diagnostics.
12. `udb_lifecycle.c.inc`: startup, publication, shutdown, durable state.

Logical data is split into six blocks, but **READY is a property of the complete set**, not of a single block.

| Block | Name | Main purpose |
|---|---|---|
| `N` | Nicks | Nick registration, authentication, attributes |
| `C` | Channels | Registered channels, access, channel policy |
| `I` | IPs | Clone limits, exemptions, host policy |
| `S` | Settings | Global settings and propagator selection |
| `L` | Links | Local per-server options |
| `K` | Lines | G/Z/Shun/Q and spamfilters |

## 3. Data model and paths

Each block owns a `UdbRecord` tree. Path components are separated by `::` and canonically encoded when they contain reserved bytes. The encoder turns `:`, `%`, control/space bytes (`<= 0x20`) and non-ASCII bytes (`>= 0x7f`) into uppercase `%HH` sequences.

Conceptual examples:

```text
N::alice::pass
C::#chat::founder
C::#chat::access::alice
I::203.0.113.20::clones
S::propagator
K::F::<pattern>::action
```

On-disk block files omit the block letter because the file itself identifies the block:

```text
alice::pass sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
alice::modes +iw
```

Numeric values are serialized as `*<integer>`; strings are written literally after the first space. Numeric parsing is strict: no sign, whitespace, or trailing characters are accepted.

### 3.1 Representation limits

The implementation enforces, among others:

- encoded logical path: **8192 bytes**;
- raw component: **4608 bytes**;
- encoded component: **4608 bytes**;
- record value: **4096 bytes**;
- snapshot `txid`: **31 characters**;
- decoded spamfilter pattern: **3072 bytes**;
- each logical record must also fit an UnrealIRCd S2S line with an internal 256-byte overhead budget;
- top-level hash: dynamically sized per block, with a 2048-bucket minimum;
  bucket counts are powers of two and grow with root-profile load.

A record fitting on disk is not sufficient; it must also satisfy the S2S bound so it can be safely synchronized.

## 4. Six-block schema

### 4.1 N block — Nicks

General form:

```text
N::<nick>::<key>
```

Allowed keys:

| Key | Type | Current runtime effect |
|---|---|---|
| `access` | string | Comma/space separated CIDRs from which the nick may be used. Missing means no IP restriction. |
| `pass` | string | Password hash. |
| `challenge` | string | Forces auth type: `argon2id`, `sha256`, or `crypt`. |
| `vhost` | string | Vhost applied to an identified user. |
| `forbid` | string | Prevents nick use; hot sync may force a rename. |
| `suspend` | string | Allows the nick after required `pass` authentication, but grants no account/`+r` or UDB effects. |
| `oper` | string | Local operclass name to grant. |
| `modes` | string | Valid user modes; `o` is explicitly forbidden here. |
| `snomasks` | string | Snomasks to apply. |
| `swhois` | string | UDB-owned SWHOIS. |

Accepted password forms:

```text
argon2id:$argon2id$...
sha256:<64 hex>
crypt:<hash>
```

A raw `$argon2id$...` is also recognized when no `challenge` is set. SHA-256 compares the hexadecimal SHA-256 of the supplied password.

Password failure throttling uses 256 slots conceptually keyed by profile/IP. The default is `5:60`; it can be configured with `udb::password-flood` and overridden at runtime by `S::flood`.

#### Using a registered nick

Normal authentication:

```text
/NICK alice:Password
```

Forced collision recovery:

```text
/NICK alice!Password
/GHOST alice Password
```

If the profile has `access`, a valid password is **not sufficient**: the client IP must match at least one configured CIDR.

On successful identification UDB sets `account=<nick>` and `+r`, then applies suspension, vhost, operclass, modes, SWHOIS, and snomasks. Leaving the profile removes UDB-owned state, including an oper grant owned by UDB.

During a hot replacement of N, `+r` alone is not trusted: the current account must match the profile. Otherwise profile vhosts/opers are not applied and, when a password record exists, the current nick holder may be renamed.


`N::forbid` is exclusive: inserting it atomically removes all sibling profile properties, and no other property may be inserted until `forbid` is deleted. The normal NICK override reports its reason without a duplicate 432.

With `N::suspend`, required `pass`/`access` validation still applies before adopting the nick. A successful adopter keeps a client-local, profile-bound authentication proof but receives no account, `+r`, oper, vhost, modes, snomasks or SWHOIS. Adding it to an identified user retains that proof while stripping those UDB effects and preserving the nick. Removing `suspend` reapplies account, `+r`, and the normal profile effects without another password only when the same client still occupies the nick and the retained proof matches the current `pass`/`challenge`/`access` policy and access check. Changing any of those policy fields, leaving the nick, or disconnecting invalidates the proof.

### 4.2 C block — Channels

Forms:

```text
C::<channel>::<key>
C::<channel>::access::<nick>
```

Keys:

| Key | Type | Current runtime effect |
|---|---|---|
| `founder` | nick | Identified founder; receives UDB-owned `+q`. |
| `modes` | string | Channel modes/parameters validated against loaded handlers. |
| `topic` | string | UDB-managed topic. |
| `access` | container | Authorized nickname list. |
| `forbid` | string | Rejects JOIN with the stored reason. |
| `suspend` | string | Suppresses registered-channel/founder behavior. |
| `options` | numeric | Bitmask described below. |

`C::<channel>::access::<nick>` may carry a numeric or string value according to the schema, but **the current JOIN hook only checks that the child exists and that the user has `+r`**. The child value is not interpreted as a rank.

Options:

| Bit | Value | Name | Behavior |
|---|---:|---|---|
| `0x01` | 1 | `PROTECT_BANS` | A normal user cannot remove a locally tracked ban created by another user; founder and oper bypass it. |
| `0x02` | 2 | `LOCK_MODES` | Blocks local mode changes except list modes `b`, `e`, `I`. |
| `0x04` | 4 | `LOCK_TOPIC` | Blocks local topic changes. |
| `0x08` | 8 | `PERSISTENT` | Applies native `+P` if that channel mode exists; UDB does not emulate it. |

Bits may be combined; `*15` enables all four.

#### JOIN, founder and native key

The founder is identified only when the nickname matches `founder` and the user has `+r`. The founder can bypass JOIN bans/keys/invite and receives `+q` unless the profile has `suspend`.

`C::modes` is the key source: `+k` uses UnrealIRCd native semantics, including exact comparison. UDB covers only the first-JOIN gap before the mode is materialized; later joins are enforced by UnrealIRCd.

Protected-ban ownership is tracked in channel runtime memory. The owner is not a persistent C record.

### 4.3 I block — IPs

Forms:

```text
I::<ip-or-realhost>::clones *N
I::<ip-or-realhost>::nolines <flags>
I::<ip-or-realhost>::host <vhost>
```

Keys:

- `clones`: maximum simultaneous connections for that IP; zero is not used as an effective limit.
- `nolines`: up to 16 characters from `GZQSTmc`; UDB creates an owned TKL exception. Lowercase `c` also explicitly bypasses connect-flood in the pre-connect hook.
- `host`: explicit host/vhost override for matching clients.

Although the root validator accepts host/IP text, **runtime lookup is exact** against `client->ip` and then `realhost`; I root keys are not CIDR matching rules.

Clone handling checks `I::<ip>::clones` first. If no specific limit is active, it checks `S::clones`. It counts connected users with the same IP and rejects once the configured limit is already reached; the default text is `Too many connections from your IP`, overridden by `S::quit_clones`.

#### Derived vhost

With both `S::encryption_key` and `S::suffix`, UDB can derive a stable vhost from:

```text
HMAC-SHA256(key, "UDB-vhost-v1|<ip>|<realhost>")
```

The first 16 HMAC bytes become 32 lowercase hex characters followed by `suffix`. `encryption_key` must be exactly 64 hex characters; `suffix` must start with `.` and obey hostname restrictions.

An active `I::host` is an explicit override and is not replaced by a derived vhost. UDB saves prior host state so it can restore it when its effect is removed.

### 4.4 S block — Settings

S only accepts depth-1 keys:

| Key | Type | Current use |
|---|---|---|
| `clones` | numeric | Global clone fallback consumed by I. |
| `quit_ips` | string | Stored in context; **no IP-limit disconnect consumer exists in the current sources**. |
| `quit_clones` | string | Clone rejection message. |
| `flood` | `attempts:seconds` | Overrides password failure throttling; deletion restores local config. |
| `encryption_key` | 64 hex | HMAC key for derived vhosts. |
| `suffix` | hostname starting `.` | Derived-vhost suffix. |
| `nickserv` | `nick!user@host` mask | Preferred source for NickServ notices. |
| `chanserv` | `nick!user@host` mask | Preferred source for ChanServ actions/notices. |
| `ipserv` | `nick!user@host` mask | Preferred source for IP/vhost notices. |
| `propagator` | server list | Distributed authority/failover policy. |

Service masks select a connected **ULine** user only when exactly one user matches. No match or multiple matches fall back to the local server source and generate a fallback log.

`S::propagator` accepts an ordered comma-separated list:

```text
S::propagator udb-a.example.net,udb-b.example.net,udb-c.example.net
```

The first usable candidate wins.

### 4.5 L block — Links

Form:

```text
L::<servername>::options *N
```

Only one bit is defined:

```text
0x01 = DEBUG
```

Local debug state is specifically read from `L::<me.name>::options`. Enabling it activates UDB debug flow and removes the filter that normally prevents UDB logs from being duplicated to snomask/oper log destinations.

Unknown bits cause the effect to be ignored with a warning.

### 4.6 K block — Lines

Supported root types:

- `G`: global G-Line/server ban.
- `Z`: global Z-Line.
- `S`: global Shun.
- `Q`: global name/Q-Line.
- `F`: global spamfilter.

For G and S, the pattern uses the required `<user>@<host>` mask. Z accepts only canonical IP addresses or CIDR networks (no hostnames, users, aliases, or CIDR host bits); Q accepts a native name-ban mask. Each profile has exactly one representation:

```text
K::G::<user@host>::reason <text>
K::Z::<ip-or-network>::reason <text>
K::S::<user@host>::reason <text>
K::Q::<nickmask>::reason <text>
K::<type>::<pattern>::expires *<unix_timestamp>
```

The pattern node is a container and has no direct value. `expires` is one absolute Unix timestamp chosen by the origin; its absence is the only representation of a permanent line. `expires *0` is invalid. A timestamp at or before the local time is never materialized as a TKL.

Expiry is not derived from the TKL runtime state. The authority sweeps expired K profiles and performs the canonical transactional `DEL K::<type>::<pattern>`, which removes the complete subtree from memory and `udb_K.db`; followers remove only their local runtime TKL and send an `EXP <path> <expected-expires>` compare-and-delete request. Therefore restart, reload, snapshot, reconnect, or a change to a profile property cannot renew or resurrect a temporary line. Only an explicit `INS ...::expires` changes its lifetime; `DEL ...::expires` converts a valid remaining profile to permanent.

UDB tags managed TKLs with the reserved `set_by="UDB:managed"` marker and only removes matching UDB-owned lines.

Spamfilter F is a dynamic native Spamfilter profile. Its pattern is **always** canonical RFC4648 Base64 (`b64:` prefix); raw patterns are rejected. The decoded pattern is byte-exact and case-sensitive as an identity, is limited to 3072 bytes, and may not contain NUL:

```text
K::F::<b64-pattern>::match-type <regex|simple>
K::F::<b64-pattern>::targets <canonical-target-letters>
K::F::<b64-pattern>::action <dynamic-action>
K::F::<b64-pattern>::ban-time *<seconds>
K::F::<b64-pattern>::reason <text>
K::F::<b64-pattern>::expires *<unix_timestamp>
```

`match-type` is explicit: `regex` compiles PCRE; `simple` uses UnrealIRCd's native wildcard matcher. `targets` must use the native canonical order `cpnNPqduatTR` (channel/private/private-notice/channel-notice/part/quit/dcc/user/away/topic/message-tag/raw), with no duplicates. `action` is accepted only when UnrealIRCd recognizes it as a dynamic, non-config-only action. `ban-time` is optional, positive, and is the duration of a sanction generated by a match; it is distinct from the rule's `expires`. A partial F profile is safely inert; a complete invalid candidate is rejected before persistence and cannot replace an active rule.


### 4.6.1 IPv4, IPv6, CIDR, and UDB path encoding

This section concerns an IPv4/IPv6 value **inside a Block K policy**. It does not describe IPv6 listeners, sockets, S2S transport, server addresses, or `listen {}` configuration.

#### Logical identity, UDB component, and wire path

`::` separates UDB path components. Consequently `:` is reserved inside a component. The current codec encodes `:`, `%`, control/space bytes, and non-ASCII bytes; it emits uppercase hexadecimal escapes. It leaves printable `@` and `/` literal. Therefore every `:` in an IPv6 text component is written as `%3A`; neither `@` nor `/` needs escaping in the forms shown below.

These are three different representations of the same value:

```text
Logical identity:  2001:db8::1
UDB component:     2001%3Adb8%3A%3A1
Protocol path:     K::Z::2001%3Adb8%3A%3A1::reason
```

Percent-encoding belongs only to the UDB path. UDB decodes the component before validation and TKL materialization, so UnrealIRCd receives `2001:db8::1`, not `%3A`. A literal IPv6 string is not valid in a `DB INS`/`DB DEL` path: its colons would be interpreted as path syntax (or an invalid single colon).

| Case | Logical value | UDB component/path |
|---|---|---|
| IPv4 Z | `198.51.100.10` | `K::Z::198.51.100.10` |
| IPv4 CIDR Z | `198.51.100.0/24` | `K::Z::198.51.100.0/24` |
| IPv6 Z | `2001:db8::1` | `K::Z::2001%3Adb8%3A%3A1` |
| IPv6 CIDR Z | `2001:db8:1234::/48` | `K::Z::2001%3Adb8%3A1234%3A%3A/48` |
| G IPv6 | `*@2001:db8::1` | `K::G::*@2001%3Adb8%3A%3A1` |
| S IPv6 | `*@2001:db8::1` | `K::S::*@2001%3Adb8%3A%3A1` |

IPv4 contains no `:`, so these normal IPv4 forms need no percent-encoding:

```text
K::Z::198.51.100.10::reason
K::Z::198.51.100.0/24::reason
K::G::*@198.51.100.10::reason
```

#### Z/GZ-Line IPv6 and CIDR

Z has a dedicated validator. It accepts only one canonical IPv4/IPv6 address or CIDR network; hostnames and `user@address` are rejected. UDB does not normalize Z input before persistence. Instead, it parses the address and compares the submitted text with the platform `inet_ntop()` output: an alternative textual spelling is rejected. Thus `2001:db8::1` is accepted, while `2001:0db8:0000:0000:0000:0000:0000:0001` is rejected rather than rewritten. Use the canonical compressed, lowercase form emitted by `inet_ntop()`.

CIDR is also an identity. Z validates the prefix range and rejects host bits instead of masking them. For example, `2001:db8:1234:5678::1/48` is rejected; submit `2001:db8:1234::/48`. This policy applies to IPv4 and IPv6 Z networks.

```text
DB * INS K::Z::2001%3Adb8%3A%3A1234::reason :IPv6 blocked
DB * INS K::Z::2001%3Adb8%3A%3A1234::expires *<unix_timestamp>
DB * DEL K::Z::2001%3Adb8%3A%3A1234

DB * INS K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :IPv6 network blocked
DB * DEL K::Z::2001%3Adb8%3A1234%3A%3A/48
```

#### G-Line and Shun IPv6

G and S preserve the `user@host` identity; an IPv6 address belongs in the `host` portion. The UDB path encoder only changes the colons:

```text
DB * INS K::G::*@2001%3Adb8%3A%3A1234::reason :IPv6 G-Line
DB * INS K::G::baduser@2001%3Adb8%3A%3A1234::reason :IPv6 G-Line by ident
DB * DEL K::G::*@2001%3Adb8%3A%3A1234

DB * INS K::S::*@2001%3Adb8%3A%3A1234::reason :IPv6 Shun
DB * DEL K::S::*@2001%3Adb8%3A%3A1234
```

The current G/S UDB schema checks that both `user` and `host` exist, that there is exactly one `@`, and that each component is at most 127 bytes. It forwards the decoded host to UnrealIRCd's native server-ban API. It does **not** parse, normalize, or reject alternative IPv6/CIDR spellings for G/S. Therefore an IPv6 CIDR such as `*@2001%3Adb8%3A1234%3A%3A/48` passes UDB's structural validation and is forwarded as `*@2001:db8:1234::/48`, but canonical identity guarantees documented above apply only to Z. Operators should use the same canonical `inet_ntop()` spelling for G/S to avoid textual aliases until G/S-specific canonicalization is implemented.

Ready-to-send DB protocol examples:

```text
DB * INS K::Z::2001%3Adb8%3A%3A1::reason :IPv6 Z test
DB * INS K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :IPv6 network test
DB * INS K::G::*@2001%3Adb8%3A%3A1::reason :IPv6 G-Line test
DB * INS K::S::*@2001%3Adb8%3A%3A1::reason :IPv6 Shun test

DB * DEL K::Z::2001%3Adb8%3A%3A1
DB * DEL K::Z::2001%3Adb8%3A1234%3A%3A/48
DB * DEL K::G::*@2001%3Adb8%3A%3A1
DB * DEL K::S::*@2001%3Adb8%3A%3A1
```
## 5. `udb {}` configuration

Typical minimal configuration:

```text
loadmodule "third/udb";

udb {
    propagator "ares-services.example.net";
};
```

Accepted directives:

| Directive | Range / format | Default |
|---|---|---|
| `propagator` | one valid server name | unset |
| `password-flood` | `attempts:seconds`, both > 0 | `5:60` |
| `max-staged-records` | 1..10,000,000 | 500,000 |
| `max-staged-bytes` | 1,024..1,073,741,824 bytes | 64 MiB |
| `sync-inactivity-timeout` | 1..86,400 s | 60 s |
| `sync-absolute-timeout` | 1..86,400 s | 300 s |
| `stale-timeout` | 1..604,800 s | 300 s |

Unknown directives are configuration errors.

### 5.1 Propagator precedence

Policy is resolved in this order:

1. local `udb::propagator` from `unrealircd.conf`;
2. committed database `S::propagator`;
3. during startup, the loaded but unpublished S candidate;
4. no policy.

There is a deliberate difference: local `udb::propagator` validates **one server**, while `S::propagator` accepts an **ordered failover list**.

A remote candidate is selectable only when directly linked to the current node; when protocol availability is required it must also have confirmed HEL. A non-adjacent server does not become this node's snapshot source merely because it exists in the global topology.

## 6. Persistence and atomicity

Each block is stored at:

```text
PERMDATADIR/udb_N.db
PERMDATADIR/udb_C.db
PERMDATADIR/udb_I.db
PERMDATADIR/udb_S.db
PERMDATADIR/udb_L.db
PERMDATADIR/udb_K.db
```

Files are stored under `PERMDATADIR` and created mode `0600`.

Snapshot header:

```text
; UDB Block N - Version 1
; Generation: 12345
; Saved: 1780000000
; Records: 42
```

`Records` counts persisted logical lines, not container nodes. The checksum excludes headers and file ordering. It is CRC32 over logical `path value\n` lines sorted lexicographically. An empty tree has checksum `0`.

### 6.1 Single-block commit

Snapshot writing performs:

1. exclusive `open(<file>.tmp, O_CREAT|O_EXCL, 0600)` with `O_NOFOLLOW` where available;
2. full serialization;
3. `fflush()`;
4. file `fsync()`;
5. `rename(.tmp, file)` as the visible commit point;
6. parent-directory `fsync()`.

Failure before rename does not publish the new active state. If rename succeeded but directory fsync/close fails, the result is **COMMITTED_DURABILITY_UNCERTAIN**: memory remains aligned with the visible file, but UDB forces `BOOTSTRAPPING` and recovery.

`INS`/`DEL` mutations are copy-on-write: clone the tree, validate/apply, write the snapshot, then replace the active tree and runtime effects.

### 6.2 READY as a generation set

When entering READY all six blocks are written with one generation. Existing files are first renamed to `.udb_previous`; if any of the six writes fails, UDB attempts to restore the entire previous set.

Then `.udb_state` is atomically published:

```text
FORMAT=1
STATE=READY
ORIGIN=FRESH
GENERATION=12345
LAST_SYNC=1780000000
```

Persistent states are `READY` and `BOOTSTRAPPING`; origins are `FRESH` and `RECOVERY`.

At startup a persisted READY is accepted only if:

- `.udb_state` is syntactically valid;
- its generation is non-zero;
- all six snapshots exist and parse successfully;
- every snapshot has exactly the generation named by `.udb_state`;
- no `.udb_previous` remains from an unfinished publication.

Existing snapshots without `.udb_state`, corrupt state, an incomplete generation, or leftover backups **are not partially published**. Active roots remain empty and UDB stays NOT_READY/BOOTSTRAPPING until an authoritative database is recovered.

For a truly fresh directory, when there is no propagator policy (standalone authority) or the selected primary is the local server, UDB may persist six empty blocks and enter READY locally.

## 7. HEL 4 negotiation

Every direct UDB peer negotiates before the rest of the protocol is accepted. `HEL` is the only `DB` frame accepted before capability confirmation.

Request:

```text
:<sid> DB <peer-sid> HEL 4 <selector> <epoch> OCL [OCLG]
```

ACK:

```text
:<sid> DB <peer-sid> HEL 4 ACK <selector> <epoch> OCL [OCLG]
```

- required version: `4`;
- `epoch`: 16 lowercase hexadecimal characters identifying the OCL instance;
- `OCL`: mandatory;
- `OCLG`: optional subscription to the derived global view, typically for consumers such as Services.

Selector meaning:

- `?`: not READY/no policy; willing to use that neighbor as the exclusive bootstrap owner;
- `-`: policy exists but no usable candidate is currently selected;
- `<servername>`: selected source/propagator.

If a direct peer does not acknowledge HEL before timeout or does not support required OCL capability, UDB aborts the server link. An epoch change for the same SID is treated as a new instance and resets replay/latch state.

## 8. Authority and bootstrap model

### 8.1 With policy

The first valid candidate in policy order is selected. Snapshot imports and incoming mutations must originate from the selected direct peer with confirmed HEL.

A policy change aborts sessions/pending requests owned by a now-invalid source and reevaluates reconciliation.

### 8.2 Without policy

- Before READY, the first eligible direct peer used for bootstrap becomes the **exclusive bootstrap owner**. A later HEL from another neighbor cannot steal ownership.
- After READY, a no-policy node becomes a **standalone authority** and accepts no remote imports.

This prevents an already authoritative node from accidentally adopting a neighbor's database merely because the link exists.

## 9. Snapshot reconciliation

Reconciliation is **inventory-driven pull**, hop-by-hop. Reconciliation frames are not forwarded through the network.

Typical round:

```text
Authority                         Receiver
    |                                |
    | INF round N checksum mtime     |
    |------------------------------->|
    |                                | compares checksum
    |            RES round N         | if divergent
    |<-------------------------------|
    | BEGIN round N txid checksum    |
    |------------------------------->|
    | PUT round N txid path :value   |
    |------------------------------->|
    | ...                            |
    | END round N txid checksum      |
    |------------------------------->|
    |     ACK round N txid digest    |
    |<-------------------------------|
```

The authority advertises `INF` for **all six blocks**. The receiver may transition to READY only after:

1. N/C/I/S/L/K have all been compared in the round;
2. every divergent block has completed its snapshot;
3. no staged sessions or pending `RES` remain;
4. the complete READY generation and `.udb_state` can be durably committed.

### 9.1 Frames

```text
INF   <round> <block> <checksum> <modified_at>
RES   <round> <block>
BEGIN <round> <block> <txid> <checksum>
PUT   <round> <block> <txid> <path> :<string>
PUT   <round> <block> <txid> <path> *<number>
END   <round> <block> <txid> <checksum>
ACK   <round> <block> <txid> <digest>
ERR   <subcmd> <code> <round/correlation> <block>
```

Round IDs are non-zero decimal integers. `txid` accepts only alphanumeric characters, `-`, `_`, maximum 31 characters.

`BEGIN` requires an active reconciliation with the same authority/round and a pending `RES` for that block. `PUT` must exactly match peer/round/txid. Invalid sequencing can abort the session and the reconciliation round.

`END` recalculates the staged-tree checksum and requires it to match the received digest before persistence and commit.

### 9.2 Staging limits

Defaults:

- 500,000 staged records;
- 64 MiB accumulated `len(path)+len(data)`;
- 60 s inactivity timeout;
- 300 s absolute timeout.

The inactivity deadline is refreshed by PUT activity; the absolute deadline is not. Inventory/reconciliation and pending-RES timeouts also exist.

Failed rounds schedule bounded retries (`UDB_RECONCILE_RETRY_MAX = 6`) with backoff.

## 10. Live mutations

Authorized mutations are:

```text
INS <Block::path> <value>
DEL <Block::path>
DRP <block>
OPT <block> [modified_at]
```

Semantics:

- `INS`: insert/replace after limits and schema validation.
- `DEL`: delete a path; deleting a missing path is idempotent.
- `DRP`: drop a complete block, first persisting the empty snapshot.
- `OPT`: force block save/update and optionally relay `modified_at`.

A mutation is accepted only from the selected remote propagator. Persistence happens before runtime publication. After processing, it may be relayed hop-by-hop to HEL-confirmed direct peers, excluding the incoming direction.

Unlike `INF/RES/BEGIN/PUT/END`, mutations are intentionally capable of multihop propagation through validated re-forwarding at each node.

S2S error codes:

| Code | Name |
|---:|---|
| 1 | `NO_BLOCK` |
| 2 | `PARAMS` |
| 3 | `FATAL` |
| 4 | `SYNC_ACTIVE` |
| 5 | `NO_SYNC` |
| 6 | `FORBIDDEN` |

## 11. Readiness, health and client admission

These are separate concepts:

- `udb_ready`: publishable/usable database (`READY` vs `BOOTSTRAPPING`).
- `udb_sync_status`: synchronization health (`OK`, `DEGRADED`, `STALE`).

Rules:

- `READY + OK`: normal state.
- A READY node detecting divergence may become `DEGRADED` while reconciling and still retain a published database.
- `STALE` is only valid while **not** READY.
- A NOT_READY node starts a bootstrap-age clock; after `stale-timeout` it becomes STALE.
- Propagator unavailability is tracked separately for observability and does **not** by itself degrade a READY node.

The readiness `PRE_LOCAL_CONNECT` hook rejects **new local clients** whenever `udb_ready == 0`, with a temporary-unavailable disconnect message. This rule does not evict already-connected users.

Persistence failures that prevent durability from being proven can revoke READY even when visible content has already changed; this is deliberately fail-closed.

## 12. OCL — distributed operclass inventory

OCL is separate from the six database blocks and **is never persisted** in `udb_*.db`.

Each participating IRCd (non-ULine server) builds an inventory of loaded operclasses. An effective SHA-256 is calculated per class from:

- class name;
- `ISA` inheritance;
- ACLs, entries and variables in runtime evaluation order;
- effective parent digest.

Canonical serialization is capped at 256 KiB, ACL depth at 64, parent depth at 16, and class count at 1024.

OCL frames:

```text
DB * OCL BEGIN <originSID> <epoch> <generation> <count> <inventoryDigest>
DB * OCL ITEM  <originSID> <epoch> <generation> <name> <effectiveDigest>
DB * OCL END   <originSID> <epoch> <generation>
```

The receiver validates that `originSID` is a visible participating IRCd **reachable through the direct peer delivering the frame**. Only after a valid END and matching inventory digest does it atomically commit and relay the inventory.

Advertising a newer generation immediately removes the previous snapshot from global computation until the new one commits: fail-closed behavior. An OCL stage expires after 30 seconds. Retired epochs are remembered to reject obsolete frames.

On rehash UDB rebuilds the local inventory; if its digest is unchanged, it avoids generation churn.

## 13. OCLG — global projection

OCLG is a derived view for explicit subscribers, such as Services. IRCd UDB nodes do not need to subscribe to OCLG themselves.

The OCL registry is `READY` only when the local inventory exists and there is a current inventory for **every** visible participating server. Missing any inventory makes it `INCOMPLETE`.

When complete, OCLG is the intersection of operclasses that:

1. exist locally;
2. exist on every participant;
3. have the exact same effective digest everywhere.

Subscriber frames:

```text
DB <subscriber> OCLG BEGIN <epoch> <generation> READY|INCOMPLETE <count> <digest>
DB <subscriber> OCLG ITEM  <epoch> <generation> <name> <digest>
DB <subscriber> OCLG END   <epoch> <generation>
```

OCLG generation changes only when the effective view or READY/INCOMPLETE state changes.

## 14. Operator commands

`DBQ` and `UDB` are registered for users/servers, but a local user must be an oper.

### 14.1 `/UDB`

```text
/UDB
/UDB STATUS
/UDB OPERCLASSES [filter]
/UDB OPERCLASS <name>
```

`STATUS` reports via numeric 339:

- `READY` / `BOOTSTRAPPING`;
- `OK` / `DEGRADED` / `STALE`;
- recovery `ACTIVE` / `IDLE`;
- selected propagator and direct source;
- advertised HEL selector;
- downstream service availability;
- policy source and policy text;
- time without a usable propagator;
- new-client admission `ALLOWED` / `DENIED`;
- last successful sync.

`OPERCLASSES` shows registry completeness and per-server inventories. `OPERCLASS <name>` compares a class across all participants and can only assert global consistency when the registry is complete.

### 14.2 `/DBQ`

```text
/DBQ <block>[::path]
/DBQ <server> <block>[::path]
/DBQ STATUS
```

A block-only query returns metadata such as record count, file size, mtime, checksum and sync marker. A path query returns its value or immediate children.

#### Secret-redaction warning

Current `udb_query_is_secret()` explicitly hides:

- `N::*::pass`;
- `N::*::challenge`;
- `S::encryption_key`.


## 15. Security and invariants

The implementation follows several fail-closed rules:

- partially valid startup candidates are never published;
- snapshots are not accepted from a source that does not own the current authority round;
- staged data is not applied before checksum validation and persistence;
- mutations are schema-validated before commit;
- temporary files use exclusive creation and mode `0600`;
- `O_NOFOLLOW` is used for temporary snapshots where available;
- defensive cleanup refuses to unlink a temporary path if it is a symlink or non-regular file;
- durability uncertainty after rename revokes READY;
- incompatible HEL/OCL can close a server link rather than silently form a mixed inconsistent network.

Editing `udb_*.db` while the daemon is live is not recommended. Apart from bypassing the in-memory tree, manual edits can break generation invariants, schema, logical checksums, S2S size constraints, or the `.udb_state` generation set.

## 16. Rehash and policy changes

During rehash:

- new configuration does not prematurely erase the last known-good propagator override if rehash fails;
- after successful completion, removal of `udb::propagator` removes the local override and restores normal precedence;
- policy changes are announced internally and sessions owned by an invalid source are aborted;
- the local OCL inventory is rebuilt and, when changed, a new generation is published.

Runtime `S::propagator` changes trigger equivalent authority reevaluation after the new record/block has committed.

## 17. Build and bundle

Canonical sources: `src/`. Distribution artifact: `dist/udb.c`.

```bash
python3 scripts/bundle.py
```

Deterministically regenerates `dist/udb.c` and `modules.list`.

```bash
python3 scripts/bundle.py --check
```

Read-only verification that both artifacts exactly match current sources. The generator fails if a `.c.inc` unit is orphaned or included more than once.

Current CI builds **UnrealIRCd 6.2.6**, compiles the modular UDB source, checks bundle determinism, and runs normal plus ASan/UBSan suites covering bootstrap, staged sync, persistence, multihop, failover, OCL, limits, spamfilter, channels, clones, and schema validation.

## 18. DB protocol quick reference

```text
HEL 4 <selector> <epoch> OCL [OCLG]
HEL 4 ACK <selector> <epoch> OCL [OCLG]

INF <round> <block> <checksum> <mtime>
RES <round> <block>
BEGIN <round> <block> <txid> <checksum>
PUT <round> <block> <txid> <path> :<string>
PUT <round> <block> <txid> <path> *<number>
END <round> <block> <txid> <checksum>
ACK <round> <block> <txid> <digest>
ERR <subcmd> <code> <round/correlation> <block>

INS <Block::path> <value>
DEL <Block::path>
DRP <block>
OPT <block> [mtime]

OCL BEGIN <originSID> <epoch> <gen> <count> <digest>
OCL ITEM  <originSID> <epoch> <gen> <operclass> <digest>
OCL END   <originSID> <epoch> <gen>

OCLG BEGIN <epoch> <gen> READY|INCOMPLETE <count> <digest>
OCLG ITEM  <epoch> <gen> <operclass> <digest>
OCLG END   <epoch> <gen>
```

## 19. Recommended observability flow

To diagnose a node:

1. check `/UDB STATUS`;
2. verify the selected propagator is a direct peer with confirmed HEL 4;
3. determine whether the database is READY or in bootstrap/recovery;
4. use `/DBQ N`, `/DBQ C`, etc. to compare checksums and metadata;
5. use `/UDB OPERCLASSES` to identify missing OCL inventories;
6. use `/UDB OPERCLASS <name>` to identify definition/inheritance/ACL mismatches;
7. inspect UnrealIRCd `udb` events, especially HEL, persistence, staged sync, READY, and OCL events;
8. inspect `.udb_state` and all six generations for diagnosis only, without live editing.

## 20. Verified current limitations

At the documented commit:

- `S::quit_ips` is stored but has no runtime consumer in current sources.
- `C::<channel>::access::<nick>` values do not define ranks; child presence plus `+r` identification authorizes JOIN.
- I root keys use exact runtime lookup and are not CIDR matching rules.
- `PERSISTENT` depends on UnrealIRCd having native channel mode `+P`; UDB does not create a substitute.

These notes intentionally describe real code behavior rather than capabilities inferred from the data model.
