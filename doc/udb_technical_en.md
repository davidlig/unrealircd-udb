# UDB 4 — Technical documentation

> This document was rebuilt from the current implementation of `davidlig/unrealircd-udb` on the `main` branch and reviewed on September 11, 2026. The code, not previous versions of this documentation, was used as the source of truth.

## 1. Scope

UDB (Unreal DataBase) is a global UnrealIRCd 6 module that maintains and applies a distributed database for nickname registrations, channels, per-IP policy, settings, per-server options, and network sanctions. The module version is **4.0.0** and the distribution metadata declares **UnrealIRCd 6.2.x** as its minimum version (`min-unrealircd-version "6.2.*"`).

Canonical implementation sources live under `src/` and are deterministically amalgamated into `dist/udb.c`. The module is registered as `third/udb` and uses the S2S `DB` command for its protocol.

UDB does not implement an autonomous IRC registration service that creates accounts or channels. Authorized writes arrive through the `DB` protocol from the selected authority; each IRCd validates, persists, applies, and where appropriate relays those mutations.

## 2. Architecture

`src/udb.c` builds one compilation unit in this order:

1. `udb_store.c.inc`: record tree, paths, persistence.
2. `udb_config.c.inc`: `udb {}`, S settings, L options.
3. `udb_core.c.inc`: validation, schemas, SHA-256 state manifests, tree operations.
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
| `access` | string | Comma/space separated CIDRs from which the nick may be used; it is never identity. Missing means no IP restriction. |
| `pass` | string | Password hash and the only credential that can establish UDB identity. |
| `vhost` | string | Vhost applied to an identified user. |
| `forbid` | string | Prevents nick use; hot sync may force a rename. |
| `suspend` | string | With `pass`, allows the nick after authentication but grants no account/`+r` or UDB effects; without `pass`, it retains no authentication. |
| `oper` | string | Local operclass name to grant. |
| `modes` | string | Valid user modes; `o` is explicitly forbidden here. |
| `snomasks` | string | Snomask expression to apply (relative `+...`, `-...`, or mixed `+c-k`; bare letters are treated as relative `+...`). |
| `swhois` | string | UDB-owned SWHOIS. |

Accepted password forms:

```text
argon2id:$argon2id$...
sha256:<64 hex>
crypt:<hash>
```

The supported `N::pass` types are `argon2id`, `sha256`, and `crypt`; its prefix selects the authentication algorithm. SHA-256 compares the hexadecimal SHA-256 of the supplied password. `argon2id` is the recommended format for new credentials; `sha256` and `crypt` remain compatibility formats and should be migrated rather than provisioned for new passwords. Client connections that submit `/NICK nick:Password` or `/GHOST` must use TLS because the supplied password is present in the IRC command.

Password failure throttling uses 256 slots conceptually keyed by profile/IP. The default is `5:60`; it can be configured with `udb::password-flood` and overridden at runtime by `S::flood`. Expired entries are reused, but a full active table fails closed for a new profile/IP pair instead of evicting an active entry and permitting spray attempts.

#### Using a registered nick

The existence of an N profile does not identify its holder. A profile without `N::pass` is not password-protected: its nick may be used when `forbid` and `access` permit it, but it never grants `account=<nick>`, `+r`, vhost, operclass, modes, SWHOIS, or snomasks. Those configured records remain valid and dormant until the profile has `pass` and the user successfully authenticates. While dormant, UDB does not treat them as negative policy and does not remove equivalent state supplied by another source, including when a dormant record is deleted or a full N snapshot replaces or removes the profile.

Normal authentication:

```text
/NICK alice:Password
```

Password validation is a preflight step. A valid credential is bound to the pending nick change only: the account, `+r`, and profile effects become active after the change is confirmed, and no pending credential ever publishes identity by itself. The supplied password is one-shot and never becomes session state. If the change fails (`433`, Q-line, nick-change limits, collision), the current nick's active UDB identity is unchanged. The pending credential is discarded when the destination `pass`/`access` policy changes, the profile disappears, or another attempt starts, and a credential from an attempt that did not establish the nick can never be reused by a later forced rename. Initial registration follows the same rule, so a first `/NICK alice:Password` activates the same identity as a later nick change. When a formerly passless profile gains its first `N::pass`, `account`/`+r` supplied by another source are never accepted as its credential: only a real UDB authentication authorizes identity and profile effects.

A service-forced nick change is not UDB authentication: when a profile has `pass`, UDB materializes identity and effects only when a valid active identity exists from its own authentication flow. A forced change onto a protected nick without it is safely renamed away.

Forced collision recovery (only for a profile with `pass`):

```text
/NICK alice!Password
/GHOST alice Password
```

If the profile has `access`, a valid password is **not sufficient**: the client IP must match at least one configured CIDR. Without `pass`, `access` is only a nick-use restriction; it neither authenticates nor enables recovery/ghost ownership.

For a normal profile with `N::pass`, successful authentication sets `account=<nick>` and `+r` and enables vhost, operclass, modes, SWHOIS, and snomasks. Leaving the profile removes UDB-owned state, including an oper grant owned by UDB. Removing `pass` immediately removes UDB identity/effects; an already matching nick or residual account/`+r` cannot replace a password authentication.

During a hot replacement of N, neither account nor `+r` is trusted: continuity requires an active UDB identity whose bound `pass`/`access` digest matches the candidate profile and whose access still permits the client. Otherwise UDB removes the effects it actually owned and, when a password record exists, renames the current nick holder.


#### N runtime state and ownership

Block N separates three planes and never infers one from another:

| Plane | Meaning | Stored in |
|---|---|---|
| Authentication | The client proved `pass` + `access` for a nick | One-shot pending credential, consumed by the confirmed nick change |
| Identity | The client currently holds the profile identity and its public projection (`account=<nick>`, `+r`) | `UdbNickIdentity` marker |
| Effect ownership | Exactly which runtime state (`vhost`, modes, snomasks, SWHOIS, oper) UDB changed | `UdbNickEffects` marker |

Profile records are **desired state**: what UDB wants now. The ownership marker is **applied state**: what UDB actually changed. Cleanup never reads the profile to decide what to remove: the tree cannot describe the past because an external source may have replaced a value UDB once set.

Per-effect ownership:

- **Modes**: UDB records only the bits it changed from 0 to 1. Revoking clears exactly those bits, so a mode that was already active before authentication (and is also listed in `N::modes`) survives. A bit UDB set is owned until the identity ends even when external code sets the same bit again; UnrealIRCd has no per-source reference for a global mode bit. This is a known, documented limitation.
- **Vhost**: if the desired vhost is already active UDB claims nothing. Otherwise UDB records the applied value. On revoke UDB removes it only while the current vhost still equals the applied value; a replacement made by another source is preserved.
- **Snomasks**: `N::snomasks` accepts relative expressions (`+...`, `-...`, or mixed `+c-k`; bare letters are treated as relative `+...`). UDB validates them against UnrealIRCd snomask characters and applies them via the native `set_snomask()` interface. UDB records the applied expression and previous snomask state in `UdbNickEffects`. On revocation (or profile re-evaluation), UDB computes and applies the reverse relative expression (`+` becomes `-` and `-` becomes `+`), or restores the previous mask, without wiping externally set snomasks. External snomasks modified while identified are preserved.
- **SWHOIS**: UDB owns only entries with owner `udb`.
- **Oper**: UDB respects external oper status strictly: if a user is already an oper (`IsOper(client)`), UDB never replaces, downgrades, or claims ownership of the oper session. UDB only grants the configured `operclass` if the client was not an oper, marking it with `udb_oper_owned`. The grant bypasses default oper side-effects (such as default oper modes, snomasks, or oper vhosts) to keep ownership clean and isolated. On revocation, suspend, profile deletion, or nick change, UDB only de-opers if the session was owned by UDB (`udb_oper_owned`); an external oper is never touched. When revoking a UDB-owned oper, UDB safely cleans up oper status, decrements oper counters, and invokes UnrealIRCd oper cleanup hooks without wiping snomasks or other unrelated state.

Identity revocation is limited to `account`, `+r` and the identity marker, and it only acts when UDB holds an identity. Effect revocation is limited to state recorded in the ownership marker. A passless profile never creates an ownership marker, so "passless never applies and never removes" follows from the model instead of special branches.

Hot mutations (`INS`/`DEL`/`UPDATE`) and full N snapshots use the same reconciler: remove owned effects that are no longer desired, preserve external effects UDB never owned, apply missing desired effects, and record exactly what changed. A snapshot with an equivalent `pass`/`access` policy keeps identity and reconciles effects; a changed policy, a new `suspend`, a removal or an access denial revokes identity and effects. `INS suspend` revokes effects and identity but keeps the nick; removing `suspend` never restores identity.

External `account`/`+r` never authenticate. While UDB identity is active, an external change to them invalidates the public representation but cannot recreate authentication; revoking identity sets `account=*` and removes `+r`. When UDB never held identity, external `account`/`+r` are left untouched.

`N::forbid` is exclusive: inserting it atomically removes all sibling profile properties, and no other property may be inserted until `forbid` is deleted. The normal NICK override reports its reason without a duplicate 432.

With `N::suspend` and `pass`, required `pass`/`access` validation still applies before adopting the nick. A successful adopter keeps the nick but receives no account, `+r`, oper, vhost, modes, snomasks or SWHOIS, and retains no identity: `suspend` destroys any active UDB identity. Adding it to an identified user strips those UDB effects and preserves the nick. Removing `suspend` from a profile with `pass` never restores identity: the current holder is renamed and must authenticate again with `/NICK nick:Password`. A suspended profile without `pass` has no identity, so removing `suspend` never identifies its holder. Account/`+r` alone cannot create identity. UDB neither adds nor removes externally owned `+S`.

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

Expiry is not derived from the TKL runtime state. The root authority sweeps expired K profiles and performs the canonical transactional `DEL K::<type>::<pattern>`, which removes the complete subtree from memory and `udb_K.db`; followers remove only their local runtime TKL and send or relay an `EXP <path> <expected-expires>` compare-and-delete request toward their selected upstream. Therefore restart, reload, snapshot, reconnect, or a change to a profile property cannot renew or resurrect a temporary line. Only an explicit `INS ...::expires` changes its lifetime; `DEL ...::expires` converts a valid remaining profile to permanent.

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

`Records` counts persisted logical lines, not container nodes. The manifest digest excludes headers and file ordering. It is canonical SHA-256 over logical `path value\n` lines sorted lexicographically. An empty tree has digest `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

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

- required version: `4` (strict single version; no negotiation, downgrade shims, or legacy compatibility);
- `epoch`: 16 lowercase hexadecimal characters identifying the peer's runtime instance;
- `OCL`: mandatory runtime operclass capability;
- `OCLG`: optional subscription to the derived global view, typically for consumers such as Services.

Selector meaning:

- `?`: not READY/no policy; willing to use that neighbor as the exclusive bootstrap owner;
- `-`: policy exists but no usable candidate is currently selected;
- `<servername>`: selected source/propagator.

If a direct peer does not acknowledge HEL before timeout, UDB aborts the server link. Certain recognizable legacy request shapes that omit or misplace mandatory `OCL` are aborted immediately; other malformed HEL frames are ignored, cannot confirm the capability, and may consequently reach the timeout. These paths do not emit a DB `ERR HEL` frame. An instance epoch change for the same SID is treated as a fresh instance and resets sequence/stream latch state before a full reconciliation.

## 8. Authority, bootstrap, and freshness model

### 8.1 With policy

The first valid candidate in policy order is selected. Snapshot imports and incoming mutations must arrive through the selected direct peer with confirmed HEL. A multihop mutation retains the root origin SID/epoch in its IRC prefix and payload; the direct peer is the authorized transport hop, not necessarily the stream origin.

A policy change aborts sessions/pending requests owned by a now-invalid source and reevaluates reconciliation.

### 8.2 Without policy

- Before READY, the first eligible direct peer used for bootstrap becomes the **exclusive bootstrap owner**. A later HEL from another neighbor cannot steal ownership.
- After READY, a no-policy node becomes a **standalone authority** and accepts no remote imports.

This prevents an already authoritative node from accidentally adopting a neighbor's database merely because the link exists.

### 8.3 Freshness and rollback semantics (Policy A — Absolute authority)

UDB operates under an explicit, deterministic authority model (**Policy A — Selected propagator wins**):

1. **Absolute authority precedence**:
   - The selected authoritative propagator is the single source of truth for the replicated database state.
   - During reconciliation, when an authoritative snapshot advertises a divergent canonical SHA-256 digest, the receiver unconditionally pulls, verifies, and commits the authority's snapshot, replacing its local block state.
   - Live mutations originating from the authoritative propagator with strictly sequential sequence numbers (`seq == expected_seq`) are applied directly on top of the committed state.

2. **Rollback and overwrite semantics**:
   - Because the selected authority's state is authoritative, if the authority's dataset contains fewer records, deletes keys, or reflects an earlier logical state than a follower's uncommitted or divergent local copy, the follower accepts the authority's state and discards its local divergence.
   - This "rollback" to the authority's view is intentional and fundamental to maintaining deterministic network-wide convergence. Followers never refuse or fail an authoritative snapshot on the basis of perceived local freshness.
   - There is no ambiguous "newest timestamp wins" (LWW / Last-Write-Wins based on wall clock) or multi-master conflict resolution in UDB. Local timestamps are never evaluated for conflict resolution.

3. **Informational role of `mtime` and wall-clock timestamps**:
   - Filesystem modification times (`st_mtime`) and packet timestamp fields (`modified_at` in `INF` or `OPT`) are strictly diagnostic metadata intended for administrative visibility (e.g. operator `/DBQ <block>` queries).
   - `mtime` is never compared to resolve conflicting records, determine block ownership, or select which node's data survives.
   - Node clock skew, timezone differences, or artificial timestamp modifications (e.g. `touch`) have zero impact on synchronization, snapshot acceptance, or convergence.

4. **Tie-breaking and standalone boundaries**:
   - SIDs and peer identifiers never override an established authority relationship. If Node A is configured to follow Node B, Node B's state always wins, regardless of lexical SID ordering (`SID(A) > SID(B)` has no effect).
   - In policy-free deployments, the first directly connected peer confirmed during bootstrap becomes the exclusive bootstrap owner. Once `READY`, a policy-free node seals itself as a standalone authority and rejects incoming remote imports, preventing accidental state clobbering by newly linked neighbors.


## 9. Snapshot reconciliation

Reconciliation is **inventory-driven pull**, hop-by-hop. Reconciliation frames are not forwarded through the network.

Typical round:

```text
Authority                                     Receiver
    |                                            |
    | INF round N block sha256 mtime watermark   |
    |------------------------------------------->|
    |                                            | compares sha256 digest
    |            RES round N block               | if divergent
    |<-------------------------------------------|
    | BEGIN round N block txid sha256 watermark  |
    |------------------------------------------->|
    | PUT round N block txid path :value         |
    |------------------------------------------->|
    | ...                                        |
    | END round N block txid sha256 watermark    |
    |------------------------------------------->|
    | ACK round N block txid sha256 watermark    |
    |<-------------------------------------------|
```

The authority advertises `INF` for **all six blocks**. The receiver may transition to READY only after:

1. N/C/I/S/L/K have all been compared in the round;
2. every divergent block has completed its snapshot transfer;
3. no staged sessions or pending `RES` remain;
4. the complete READY generation and `.udb_state` can be durably committed.

### 9.1 Frames

```text
INF   <round> <block> <sha256> <record_count> <modified_at> [<watermark_seq>]
RES   <round> <block>
BEGIN <round> <block> <txid> <sha256> [<watermark_seq>]
PUT   <round> <block> <txid> <path> :<string>
PUT   <round> <block> <txid> <path> *<number>
END   <round> <block> <txid> <sha256> [<watermark_seq>]
ACK   <round> <block> <txid> <sha256> [<watermark_seq>]
ERR   <subcmd> <code> <round_or_seq> <block>
```

Parameters:
- Round IDs are non-zero decimal integers.
- `txid` accepts only alphanumeric characters, `-`, `_`, maximum 31 characters.
- `sha256` is a 64-character lowercase hexadecimal cryptographic digest of the block's canonical serialization.
- `watermark_seq` is the 64-bit monotonic sequence number representing the latest mutation included in the snapshot.
- Only the documented arities are accepted. `INF`, `BEGIN`, `END`, and `ACK` may omit the legacy-compatible optional watermark, but if present it must be a canonical unsigned decimal value.

`BEGIN` requires an active reconciliation with the same authority/round and a pending `RES` for that block. `PUT` must exactly match peer/round/txid. Invalid sequencing aborts the session and the reconciliation round.

`END` recalculates the staged-tree canonical SHA-256 digest and requires an exact match with the received digest before persistence and atomic commit.

### 9.2 Staging limits

Defaults:

- 500,000 staged records;
- 64 MiB accumulated `len(path)+len(data)`;
- 60 s inactivity timeout;
- 300 s absolute timeout.

The inactivity deadline is refreshed by PUT activity; the absolute deadline is not. Inventory/reconciliation and pending-RES timeouts also exist.

Failed rounds schedule bounded retries (`UDB_RECONCILE_RETRY_MAX = 6`) with backoff.

### 9.3 Strong identity and canonical serialization

Convergence detection relies on deterministic SHA-256 digests rather than checksums:
- Every record is serialized into canonical wire form: `<Block::path> <value>\n` (or `<Block::path>\n` when valueless).
- Records are sorted strictly in lexicographical byte order (`strcmp`).
- Empty blocks hash an empty byte string.
- No C struct padding, pointers, uninitialized bytes, or hash-table iteration dependencies are included.
- Any single-byte semantic divergence produces a completely different 64-hex SHA-256 digest.

### 9.4 Watermark and snapshot/live mutation race resolution

To eliminate race conditions between live mutations and background reconciliation snapshots:
1. Every snapshot carries `watermark_seq` indicating the exact authority sequence number reflected in the dataset. All six `INF` entries and every `BEGIN`/`END` in one round must report the same watermark; inconsistent rounds abort fail closed.
2. Upon committing the snapshot and entering `READY`, the node sets:
   - `last_applied_seq = watermark_seq`
   - `expected_seq = watermark_seq + 1`
3. Live mutations received with `seq <= watermark_seq` are discarded as duplicates (`seq <= last_applied_seq`).
4. Live mutations with `seq == watermark_seq + 1` apply cleanly on top of the committed state.
5. Live mutations with `seq > watermark_seq + 1` trigger gap detection and a fresh reconciliation.

## 10. Live mutations and sequencing

Authorized mutations originated by the selected authoritative propagator are:

```text
INS <epoch> <seq> <Block::path> <value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
OPT <epoch> <seq> <block> [modified_at]
EXP <path> <expected-expires>
```

Parameters:
- `epoch`: 16 lowercase hex characters identifying the root origin's instance epoch.
- `seq`: 64-bit decimal integer strictly monotonic within the `(root source SID, epoch)` stream (`seq >= 1`), shared across all six persistent blocks (`N`, `C`, `I`, `S`, `L`, `K`) to ensure total causal ordering.

Semantics:

- `INS`: insert/replace after limits and schema validation.
- `DEL`: delete a path; deleting a missing path is idempotent.
- `DRP`: drop a complete block, first persisting the empty snapshot.
- `OPT`: force block save/update and optionally relay `modified_at`.
- `EXP`: point-to-point compare-and-delete request for an expired K line sent toward the selected direct authority (`DB <target> EXP <path> <expected-expires>`). A relay validates and deduplicates the request, then forwards it to its own selected upstream. Only the root authority deletes the profile, persists `udb_K.db`, allocates the next stream sequence, and broadcasts the transactional `DEL`; stale requests are ignored without error.

### 10.1 Monotonic sequence rules and gap recovery

Receivers process incoming mutations against the active `(root source SID, epoch)` stream, received through the selected direct peer:

- `expected_seq = last_applied_seq + 1`:
  - **In-order (`seq == expected_seq`)**: Validate -> persist locally -> publish to runtime -> advance `last_applied_seq = seq` -> relay multihop to downstream confirmed peers.
  - **Duplicate / Stale (`seq <= last_applied_seq`)**: Safely ignored without re-applying or mutating active state.
  - **Gap / Out-of-order (`seq > expected_seq`)**: Gap detected. The node suspends live application, marks sync health `DEGRADED`, and triggers an immediate staged reconciliation round (`RES`) to heal the gap.
  - **Stream mismatch (source SID or epoch differs)**: The packet is not applied. Its stream becomes a candidate and a full six-block reconciliation runs; only a successful round promotes the candidate and adopts that round's exact watermark, which may legitimately be lower than the previous stream's watermark.
  - **Persistence failure**: If persisting an in-order mutation fails (e.g. disk I/O error), the node marks sync health `DEGRADED` and initiates reconciliation pull, ensuring local storage failures cannot cause silent network divergence.

Unlike `INF/RES/BEGIN/PUT/END`, mutations are intentionally capable of multihop propagation through validated re-forwarding at each node.

### 10.2 Periodic anti-entropy verification

To detect silent divergence between peers without waiting for network events or link restarts, healthy nodes periodically verify state with their direct authority:

Frames:

```text
MANIFEST REQ <round>
MANIFEST ACK <round> <block> <count> <sha256> <watermark_seq>
```

Workflow:
1. Every `anti-entropy-interval` (default 300 s ± 30 s jitter), a follower sends `MANIFEST REQ` to its selected authority.
2. The authority replies with `MANIFEST ACK` for each of the six blocks, reporting its current `(count, sha256, watermark_seq)`. All six acknowledgements must carry one identical watermark; an inconsistent set fails closed and forces capability refresh/recovery.
3. The follower compares the authority's manifests with its local active block manifests.
4. **Match**: All blocks match; the state is verified convergent. No data is transferred and no noisy logs are produced.
5. **Mismatch**: Any count or SHA-256 divergence immediately marks health `DEGRADED` and requests reconciliation pull (`RES`) for the divergent block.
6. Rate limiting and single in-flight checks prevent synchronization storms.

### 10.3 S2S error codes

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

A block-only query returns metadata such as record count, file size, mtime, SHA-256 digest and sync marker. A path query returns its value or immediate children.

#### Secret-redaction warning

Current `udb_query_is_secret()` explicitly hides:

- `N::*::pass`;
- `S::encryption_key`;
- the complete `C::<channel>::modes` value, because it may contain a native `+k` key.

Mode debug notices likewise replace all parameters with `<redacted>` whenever the mode expression contains `k`.


## 15. Security and invariants

The implementation follows several fail-closed rules:

- partially valid startup candidates are never published;
- snapshots are not accepted from a source that does not own the current authority round;
- staged data is not applied before canonical SHA-256 digest validation and persistence;
- mutations are schema-validated before commit;
- temporary files use exclusive creation and mode `0600`;
- `O_NOFOLLOW` is used for temporary snapshots where available;
- defensive cleanup refuses to unlink a temporary path if it is a symlink or non-regular file;
- durability uncertainty after rename revokes READY;
- incompatible HEL/OCL can close a server link rather than silently form a mixed inconsistent network.

### 15.1 Trust and transport boundary

HEL 4 authenticates neither a server nor a payload. UDB trusts UnrealIRCd's authenticated server link and then applies its own direct-peer authority policy, sequencing, staging, and schema checks. Every UDB hop should therefore use TLS with certificate verification and tightly controlled link credentials. Without TLS, the DB protocol exposes full snapshots and live values in transit, including password hashes, encryption material, and channel keys; integrity also depends on the underlying link authentication. UDB provides no end-to-end encryption or signature above the server link.

Editing `udb_*.db` while the daemon is live is not recommended. Apart from bypassing the in-memory tree, manual edits can break generation invariants, schema, canonical digests, S2S size constraints, or the `.udb_state` generation set.

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

INF <round> <block> <sha256> <record_count> <mtime> [<watermark_seq>]
RES <round> <block>
BEGIN <round> <block> <txid> <sha256> [<watermark_seq>]
PUT <round> <block> <txid> <path> :<string>
PUT <round> <block> <txid> <path> *<number>
END <round> <block> <txid> <sha256> [<watermark_seq>]
ACK <round> <block> <txid> <sha256> [<watermark_seq>]
ERR <subcmd> <code> <round/correlation> <block>

INS <epoch> <seq> <Block::path> <value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
OPT <epoch> <seq> <block> [mtime]
EXP <path> <expected-expires>

MANIFEST REQ <round>
MANIFEST ACK <round> <block> <count> <sha256> <watermark_seq>

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
4. use `/DBQ N`, `/DBQ C`, etc. to compare SHA-256 digests and metadata;
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
