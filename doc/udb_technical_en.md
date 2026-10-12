# UDB — Technical documentation

> This document describes the current canonical `src/` implementation. Module versioning is independent of HEL 4 and native storage generation 4. Deployment requirements and current limitations are documented below; see the [test guide](../tests/README.md) for validation scope.

## 1. Scope

UDB (Unreal DataBase) is a global UnrealIRCd 6 module that maintains and applies a distributed database for nickname registrations, channels, per-IP policy, settings, per-server options, and network sanctions. The module version is **4.0.0-rc1** and the distribution metadata declares **UnrealIRCd 6.2.x** as its minimum version (`min-unrealircd-version "6.2.*"`).

Canonical implementation sources live under `src/` and are deterministically amalgamated into `dist/udb.c`. The module is registered as `third/udb` and uses the S2S `DB` command for its protocol.

UDB does not implement an autonomous IRC registration service that creates accounts or channels. Authorized writes arrive through the `DB` protocol from the selected authority; each IRCd validates, persists, applies, and where appropriate relays those mutations.

## 2. Architecture

`src/udb.c` composes one translation unit. `udb_storage*.c.inc` implements the
native binary engine; `udb_native*.c.inc` provides frozen schema, typed reads,
wire codecs, staged send/receive and exact-generation runtime effects.
`udb_store.c.inc` contains path encoding/secret classification only.
`udb_core.c.inc` contains pure business schemas and parsing. The configuration,
services, HEL/OCL, mutation, protocol, N/C/I/K consumers, diagnostics and lifecycle
units coordinate foreground daemon behavior. `dist/udb.c` is generated.
READY belongs to all six blocks N/C/I/S/L/K, not to an individual root.

## 3. Data model and paths

Logical paths use `Block::component::property`, for example `N::alice::pass`,
`C::#chat::access::alice`, `I::203.0.113.20::clones` and `S::propagator`.
Components percent-encode `:`, `%`, space/control and non-ASCII bytes as uppercase
`%HH`; literal printable `@`, `/` and `*` remain literal. Structural control bytes
and NUL are rejected even if escaped. Keys fold ASCII case except exact K::F patterns.
The binary row carries explicit NONE/STRING/uint64 types. Protocol values use
`!n`, `!s<Base64>` and `!u<decimal>` as explicit type tags.
Tables below describe logical values, not wire payload spellings.

| Logical type | Wire value | Example |
|---|---|---|
| NONE (entry without a value) | `!n` | `!n` |
| Unsigned integer (uint64) | `!u<canonical decimal>` | `5` → `!u5` |
| STRING (string bytes) | `!s<canonical RFC4648 Base64>` | `Hello world` → `!sSGVsbG8gd29ybGQ=` |

`!n` is a literal tag, not a number or a deletion. `!u0` is a numeric value;
`!s` is an empty string at codec level, but individual properties may reject it.
There is no separate BYTES type: STRING preserves the supplied bytes without
transcoding (for UTF-8 text, encode its UTF-8 bytes). NUL, CR and LF are rejected,
and each property's schema imposes further restrictions. Base64 is not encryption.
Numbers are canonical decimal on wire, including bitmasks. The leading `:`
before a payload introduces the trailing IRC
parameter and is not part of the typed value. Paths use `%HH`, not Base64;
see [the complete services examples](#104-s2s-examples-for-irc-services).

### 3.1 Representation limits

- At most 8 structural components; business schemas currently allow at most 3.
- Raw logical path under 8192 bytes; each raw component at most 4608 bytes.
- Value at most 4096 bytes; snapshot txid at most 31 characters.
- Decoded spamfilter pattern at most 3072 bytes.
- Every actual wire row must additionally fit MAXLINELENGTH minus the reserved
  256-byte S2S overhead; percent/Base64 expansion counts toward that bound.
- Logical counts and numbers use uint64 with checked arithmetic and explicit LE encoding.

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

During a hot replacement of N, neither account nor `+r` is trusted: continuity requires an active UDB identity for that nick, the candidate profile to keep `pass`, and the candidate `access` to still permit the client. Changing the `pass` value, or an `access` list that still permits the holder, does not revoke identity. Otherwise UDB removes the effects it actually owned and, when the holder has no valid identity and a password record exists, renames the current nick holder.


#### N runtime state and ownership

Block N separates three planes and never infers one from another:

| Plane | Meaning | Stored in |
|---|---|---|
| Authentication | The client proved `pass` + `access` for a nick | One-shot pending credential, consumed by the confirmed nick change |
| Identity | The client currently holds the profile identity and its public projection (`account=<nick>`, `+r`) | `UdbNickIdentity` marker |
| Effect ownership | Exactly which runtime state (`vhost`, modes, snomasks, SWHOIS, oper) UDB changed | `UdbNickEffects` marker |

Profile records are **desired state**: what UDB wants now. The ownership marker is **applied state**: what UDB actually changed. Cleanup never reads the profile to decide what to remove: the current profile cannot describe the past because an external source may have replaced a value UDB once set.

Per-effect ownership:

- **Modes**: UDB records only the bits it changed from 0 to 1. Revoking clears exactly those bits, so a mode that was already active before authentication (and is also listed in `N::modes`) survives. A bit UDB set is owned until the identity ends even when external code sets the same bit again; UnrealIRCd has no per-source reference for a global mode bit. This is a known, documented limitation.
- **Vhost**: if the desired vhost is already active UDB claims nothing. Otherwise UDB records the applied value. On revoke UDB removes it only while the current vhost still equals the applied value; a replacement made by another source is preserved.
- **Snomasks**: `N::snomasks` accepts relative expressions (`+...`, `-...`, or mixed `+c-k`; bare letters are treated as relative `+...`). UDB validates them against UnrealIRCd snomask characters and applies them via the native `set_snomask()` interface. UDB records the applied expression and previous snomask state in `UdbNickEffects`. On revocation (or profile re-evaluation), UDB computes and applies the reverse relative expression (`+` becomes `-` and `-` becomes `+`), or restores the previous mask, without wiping externally set snomasks. External snomasks modified while identified are preserved unless UDB de-opers an oper it owns, in which case UnrealIRCd's native de-oper cleanup clears snomasks.
- **SWHOIS**: UDB owns only entries with owner `udb`.
- **Oper**: UDB respects external oper status strictly: if a user is already an oper (`IsOper(client)`), UDB never replaces, downgrades, or claims ownership of the oper session. UDB only grants the configured `operclass` if the client was not an oper, marking it with `udb_oper_owned`. The grant uses UnrealIRCd's native global oper defaults (`set::modes-on-oper`, `set::snomask-on-oper`, `set::oper-vhost`, and `set::oper-auto-join`); an explicit `N::vhost` takes precedence and remains a literal profile value rather than an expandable `ident@host` oper template. On revocation, suspend, profile deletion, or nick change, UDB only de-opers if the session was owned by UDB (`udb_oper_owned`); an external oper is never touched. Revocation performs native de-oper cleanup and removes/restores an oper vhost only when the UDB grant actually changed it and the applied value has not subsequently been replaced externally. Channels joined through `set::oper-auto-join` are not parted on de-oper, matching UnrealIRCd behavior.

Identity revocation is limited to `account`, `+r` and the identity marker, and it only acts when UDB holds an identity. Effect revocation is limited to state recorded in the ownership marker. A passless profile never creates an ownership marker, so "passless never applies and never removes" follows from the model instead of special branches.

After an explicit `/NICK nick:Password` adoption successfully materializes identity and effects, UDB sends `You are now identified for nickname <nick>.` as a NickServ notice. Profile refreshes and suspended adoptions do not emit that success notice.

Hot mutations (`INS`/`DEL`/`UPDATE`) and full N snapshots use the same reconciler: remove owned effects that are no longer desired, preserve external effects UDB never owned, apply missing desired effects, and record exactly what changed. A snapshot that keeps `pass` and an `access` list that still permits the holder keeps identity and reconciles effects, even when the values change; a new `suspend`, a removal, losing `pass` or an access denial revokes identity and effects. `INS suspend` revokes effects and identity but keeps the nick; removing `suspend` never restores identity.

External `account`/`+r` never authenticate. While UDB identity is active, an external change to them invalidates the public representation but cannot recreate authentication; revoking identity sets `account=*` and removes `+r`. When UDB never held identity, external `account`/`+r` are left untouched.

`N::forbid` is exclusive: inserting it atomically removes all sibling profile properties, and no other property may be inserted until `forbid` is deleted. The normal NICK override reports its reason without a duplicate 432.

With `N::suspend` and `pass`, required `pass`/`access` validation still applies before adopting the nick. A successful adopter keeps the nick but receives no account, `+r`, oper, vhost, modes, snomasks or SWHOIS, and retains no identity: `suspend` destroys any active UDB identity. Adding it to an identified user strips those UDB effects and preserves the nick. Removing `suspend` from a profile with `pass` never restores identity: the current holder is renamed and receives the NickServ notices `The nickname <nick> is no longer suspended.` followed by `You have been renamed. If you are the owner, please identify: /NICK <nick>:Password` before authenticating again. A suspended profile without `pass` has no identity, so removing `suspend` keeps its holder in place and never identifies it. Account/`+r` alone cannot create identity. UDB neither adds nor removes externally owned `+S`.

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
| `modes` | string | Channel modes/parameters validated against loaded handlers. The registration marker `r`, member ranks (`q`, `a`, `o`, `h`, `v`), `P`, and `O` are rejected in mutations, staged sync, and persisted files; persistence and oper-only policy are expressed only by their `options` bits. |
| `topic` | string | UDB-managed topic. |
| `access` | container | Authorized nickname list. |
| `forbid` | string | Rejects JOIN with the stored reason. |
| `suspend` | string | Suppresses registered-channel/founder behavior: while present it removes native `+r` and founder `+q`; lifting it restores both on a live channel. The stored value is reported as the reason to every local user who joins the channel while it is suspended. |
| `options` | numeric | Bitmask described below. |

`C::<channel>::access::<nick>` may carry NONE, a numeric value or a non-empty string according to the schema, but **the current JOIN hook only checks that the child exists and that the user has `+r`**. The child value is not interpreted as a rank.

Options:

| Bit | Value | Name | Behavior |
|---|---:|---|---|
| `0x01` | 1 | `PROTECT_BANS` | A normal user cannot remove a locally tracked ban created by another user; founder and oper bypass it. |
| `0x02` | 2 | `LOCK_MODES` | Blocks local mode changes except list modes `b`, `e`, `I` and member ranks `q`, `a`, `o`, `h`, `v`. |
| `0x04` | 4 | `LOCK_TOPIC` | Blocks local topic changes. |
| `0x08` | 8 | `PERSISTENT` | Applies native `+P` if that channel mode exists; UDB does not emulate it. |
| `0x10` | 16 | `OPER_ONLY` | Restricts JOIN to IRC operators and applies native `+O` when its handler exists. JOIN remains fail-closed if the handler is absent. |
| `0x20` | 32 | `SECURE_OPS` | Only a `+q` member may grant ranks, except for the isolated founder self-recovery described below. Other members may remove ranks below their highest rank, or their own rank when UnrealIRCd permits it. `SAMODE` retains native administrator access. |

Bits combine with bitwise OR; logical value `63` enables all six (wire `!u63`). With `SECURE_OPS` unset, native rank permissions are unchanged apart from the founder self-recovery described below. `suspend` removes UDB-owned `+O` and lifting it restores `+O`; deleting `options` or the profile removes it. UDB always reconciles `+O/-O` before `+P/-P`, because removing `+P` may destroy an empty channel.

#### JOIN, founder and native key

The founder is identified only when the nickname matches `founder` and the user has `+r`. An identified founder bypasses JOIN restrictions, including `OPER_ONLY` and `+k`, unless the profile has `suspend` or `forbid`; the founder receives `+q` unless suspended. An IRC operator bypasses all UDB JOIN restrictions, including `forbid`, `OPER_ONLY`, and the stored first-JOIN `+k`, but independent native restrictions still run and require their own override permission. `suspend` retains its existing non-blocking JOIN behavior.

If identification occurs after JOIN, `/MODE #channel +q nickname` lets the local identified founder recover their own `+q` without rejoining. UDB requires channel membership, a non-suspended C profile, the current founder nick with user mode `+r`, and exactly the isolated `+q` mode with one parameter resolving to the issuer. The recovery works with or without `SECURE_OPS` and `LOCK_MODES`; repeating it while already holding `+q` emits no additional mode change. Other targets, compound or malformed commands, and `SAMODE` keep their existing handling. Removing a nick suspension still requires reauthentication; this command never authenticates the user.

UDB removes native `+O` only when it applied that mode. If the native handler unloads, UDB clears its ownership before UnrealIRCd removes the mode directly; when the handler returns, active `OPER_ONLY` channels regain `+O`. An independent `+O`, including one set after another actor removes UDB's `+O` or after the handler returns, survives unrelated `options` changes and UDB option removal.
For UDB-owned `+O`, any IRC operator may join even if their operclass lacks UnrealIRCd's `channel:operonly:join` permission; all other native JOIN checks still run. An independently set native `+O` retains UnrealIRCd's operclass permission requirement.

UDB owns native `+r` as the registered-channel marker: registering a live channel applies it, `suspend` and profile deletion remove it, and lifting `suspend` restores it together with founder `+q`. `C::modes` never stores `r`, member ranks, `P`, or `O`.

Before the first JOIN materializes `C::modes`, UDB enforces stored `+i`, `+R`, `+z`, and `+k` with their native numerics and exact key comparison. Later joins use UnrealIRCd's native handlers. `/INVITE` and a materialized `+I` retain their native semantics, but an invitation cannot bypass UDB `forbid` or `OPER_ONLY`.

`O` is no longer valid in `C::modes`. There is no automatic migration: convert any existing stored `C::modes +O` record to the `0x10` `OPER_ONLY` option before loading this version.

Protected-ban ownership is tracked in channel runtime memory. The owner is not a persistent C record.

### 4.3 I block — IPs

Forms:

```text
I::<ip-or-realhost>::clones <uint64>
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

#### Host privacy and custom vhosts

UDB uses the derived `.virtual` host as the base cloak (`+x`, without `+t`), not as a removable vhost. Explicit `N::vhost`, `I::host` and oper hosts are overlays (`+xt`); `-t` returns to the protected base. A local user can restore their current `N::vhost` with `+t` only while authenticated by UDB, with `+r`, valid password/access state and no suspend/forbid. External `+r` alone is insufficient. Native host-change restrictions and flood limits still apply. A user may deliberately remove their own `+x` with `MODE` or `UMODE2`, exposing their real host; `+x` restores the retained UDB base, not the native cloak or N vhost. UnrealIRCd's native authorization controls changes by other actors, including services; UDB adds no `-x` restriction. Refreshing the base (including during reload) preserves the current `x` state; materializing a custom profile vhost still enables `+xt`.

Disconnect never changes the chosen host visibility: cloaked users remain cloaked, while voluntary `-x` remains explicit disclosure. Realhost/IP remain available internally for matching and authorized diagnostics. If the derivation key/suffix disappears, existing users retain their protected base; new users use a validated native cloak or are rejected if none is safe. Base hostnames are synchronized as origin-owned early client ModData, without keys or a DB protocol change.

UDB can be reloaded but cannot be hot-unloaded. Upgrade every participating IRCd. When replacing the installed module, prefer a controlled restart. These protections concern ordinary users' public host display, not trusted server transport or authorized realhost/IP diagnostics.

`N::modes` does not own `x` or `t`; host state is managed separately.

### 4.4 S block — Settings

S only accepts depth-1 keys:

| Key | Type | Current use |
|---|---|---|
| `clones` | numeric | Global clone fallback consumed by I. |
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
L::<servername>::options <uint64>
```

Only one bit is defined:

```text
0x01 = DEBUG
```

Local debug state is specifically read from `L::<me.name>::options`. Enabling it activates UDB debug flow and removes the filter that normally prevents UDB logs from being duplicated to snomask/oper log destinations. When the module is unloaded (for example during REHASH), the filter is re-applied for the entire unload window even if DEBUG is active, because the core may already have unloaded other modules' ModData; file log destinations are unaffected.

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
K::<type>::<pattern>::expires <unix_timestamp>
```

The pattern node is a container and has no direct value. `expires` is one absolute Unix timestamp chosen by the origin; its absence is the only representation of a permanent line. `expires = 0` is invalid. A timestamp at or before the local time is never materialized as a TKL.

Expiry is not derived from the TKL runtime state. The root authority sweeps expired K profiles and performs the canonical transactional `DEL K::<type>::<pattern>`, which removes the complete subtree from the active root and `udb_K.db`; followers remove only their local runtime TKL and send or relay an `EXP <path> <expected-expires>` compare-and-delete request toward their selected upstream. Therefore restart, reload, snapshot, reconnect, or a change to a profile property cannot renew or resurrect a temporary line. Only an explicit `INS ...::expires` changes its lifetime; `DEL ...::expires` converts a valid remaining profile to permanent.

UDB tags managed TKLs with the reserved `set_by="UDB:managed"` marker and only removes matching UDB-owned lines.

Spamfilter F is a dynamic native Spamfilter profile. Its pattern is **always** canonical RFC4648 Base64 (`b64:` prefix); raw patterns are rejected. The decoded pattern is byte-exact and case-sensitive as an identity, is limited to 3072 bytes, and may not contain NUL:

```text
K::F::<b64-pattern>::match-type <regex|simple>
K::F::<b64-pattern>::targets <canonical-target-letters>
K::F::<b64-pattern>::action <dynamic-action>
K::F::<b64-pattern>::ban-time <seconds>
K::F::<b64-pattern>::reason <text>
K::F::<b64-pattern>::expires <unix_timestamp>
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
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234::reason :!sSVB2NiBibG9ja2Vk
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234::expires :!u<unix_timestamp>
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234

:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :!sSVB2NiBuZXR3b3JrIGJsb2NrZWQ=
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48
```

#### G-Line and Shun IPv6

G and S preserve the `user@host` identity; an IPv6 address belongs in the `host` portion. The UDB path encoder only changes the colons:

```text
:<root-SID> DB * INS <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1234::reason :!sSVB2NiBHLUxpbmU=
:<root-SID> DB * INS <root-epoch> <seq> K::G::baduser@2001%3Adb8%3A%3A1234::reason :!sSVB2NiBHLUxpbmUgYnkgaWRlbnQ=
:<root-SID> DB * DEL <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1234

:<root-SID> DB * INS <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1234::reason :!sSVB2NiBTaHVu
:<root-SID> DB * DEL <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1234
```

The current G/S UDB schema checks that both `user` and `host` exist, that there is exactly one `@`, and that each component is at most 127 bytes. It forwards the decoded host to UnrealIRCd's native server-ban API. It does **not** parse, normalize, or reject alternative IPv6/CIDR spellings for G/S. Therefore an IPv6 CIDR such as `*@2001%3Adb8%3A1234%3A%3A/48` passes UDB's structural validation and is forwarded as `*@2001:db8:1234::/48`, but canonical identity guarantees documented above apply only to Z. Operators should use the same canonical `inet_ntop()` spelling for G/S to avoid textual aliases until G/S-specific canonicalization is implemented.

Ready-to-send DB protocol examples:

```text
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1::reason :!sSVB2NiBaIHRlc3Q=
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :!sSVB2NiBuZXR3b3JrIHRlc3Q=
:<root-SID> DB * INS <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1::reason :!sSVB2NiBHLUxpbmUgdGVzdA==
:<root-SID> DB * INS <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1::reason :!sSVB2NiBTaHVuIHRlc3Q=

:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48
:<root-SID> DB * DEL <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1
:<root-SID> DB * DEL <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1
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
| `cache-size` | 16 MiB..SIZE_MAX/2 bytes | 128 MiB (local) |
| `max-staged-records` | 1..10,000,000 | 10,000,000 |
| `max-staged-bytes` | 1,024..SIZE_MAX/2 bytes | 16 GiB |
| `sync-inactivity-timeout` | 1..86,400 s | 60 s |
| `sync-absolute-timeout` | 1..86,400 s | 3600 s |
| `stale-timeout` | 1..604,800 s | 300 s |
| `anti-entropy-interval` | 1..86,400 s | 1800 s |

Unknown directives are configuration errors.

### 5.1 Propagator precedence

Policy is resolved in this order:

1. local `udb::propagator` from `unrealircd.conf`;
2. committed database `S::propagator`;
3. during startup, the loaded but unpublished S candidate;
4. no policy.

There is a deliberate difference: local `udb::propagator` validates **one server**, while `S::propagator` accepts an **ordered failover list**.

A remote candidate is selectable only when directly linked to the current node; when protocol availability is required it must also have confirmed HEL. A non-adjacent server does not become this node's snapshot source merely because it exists in the global topology.

## 6. Native persistence and durability

The exclusive new layout is `PERMDATADIR/udb/`:

```text
udb_N.db  udb_C.db  udb_I.db  udb_S.db  udb_L.db  udb_K.db
udb_wal.db  udb_meta0.db  udb_meta1.db
```

The six stable binary containers hold immutable 64 KiB integrity-checked pages,
sorted fragments of at most 4 MiB, paged run indices and copy-on-write catalogs.
There is no resident full block, full key directory, text snapshot reader,
migration, or compatibility decoder. Deploy the new implementation on every node.
Never edit or partially replace these files. Backups must capture the complete
store from a stopped daemon, not independently copy live block files.

### 6.1 Bounded indexed reads

Exact reads route by a 4096-way entity partition and then a full-key SHA-256
radix tree. Leaves split above 16 MiB of encoded rows plus 128 bytes per row and
collapse when their visible content falls below that bound. Fragment/run indices
locate data without loading an entire block. Prefix operations use bounded disk
iterators. An immutable generation pin protects a reader's extents until release.
Queries distinguish FOUND, NOT_FOUND and storage errors; errors never mean absence.
Numeric zero is a value, not a missing field.

The local LRU cache defaults to 128 MiB. `udb::cache-size` takes decimal bytes
(minimum 16 MiB), may differ on every node, and is not replicated. For example,
`cache-size 268435456;` selects 256 MiB. More cache can retain more hot data/index
pages, reducing repeated I/O and validation, but does not remove startup validation
or guarantee faster bulk builds. Rehash applies the local budget; a shrink blocked
by pinned pages retains the current budget and requires a later rehash. Managed
auxiliary allocations are capped at 64 MiB, including 16 MiB of memtables and an
8 MiB total delivery/job/completion queue. Daemon-owned clients/TKLs and the OS
page cache are outside that storage budget.

### 6.2 Commit, checkpoint and recovery

A single native writer receives copied jobs with stable owner IDs and module/
authority revisions, never worker-owned `Client *` pointers. Groups target at most
5 ms or 1 MiB of queued jobs. A final authorization barrier precedes durability.
The order is data-file flush, global WAL root-set flush, immutable publication,
then foreground effects, acknowledgements and relay. Receipts pin the exact
before/after roots even when a later group is already published. Cancellation
before the barrier cannot publish; cancellation after it cannot undo a durable
transaction, but cannot grant stale READY or forwarding authority either.

Checkpoints use the inactive fixed 4 MiB metadata slot, flush/readback validation,
a durable WAL floor, then retirement of covered log entries. Extent reuse follows
a complete checkpoint-gated inventory of every active/staged/pinned generation.
Cache identities include file identity and extent generation. Maintenance output
is bounded to 64 MiB per private step. Prefix tombstones retain operation order;
a newer PUT survives an older subtree delete. DRP publishes an empty block root.

Startup validates the WAL/checkpoints and streams all referenced physical and
logical data for all six blocks before service. This costs O(total referenced data)
CPU/I/O, not O(total records) resident memory, and bypasses DATA cache admission.
A warm indexed lookup after startup is not a substitute for that validation.
Corrupt, incomplete, foreign or ambiguously provisioned stores fail closed rather
than being silently replaced with empty data. Unreferenced incomplete WAL tails
are retired only after the committed roots validate. Private orphan cleanup needs
matching store identity, integrity-checked headers, ownership and held file identity;
malformed/foreign/symlinked files are preserved. Admission stops and the writer is
joined before pins, schema snapshots, effects or storage handles are freed.

### 6.3 Platform and evidence boundary

Linux uses positioned 64-bit I/O, owner-only files, exclusive locks and real flushes.
The Windows branch uses wide paths, native positioned I/O/flushes, exclusive handles,
owner ACLs, fixed local NTFS and reparse/hardlink checks. Commit never depends on
unlink/rename or a simulated Windows directory fsync. The deployment targets are
Windows 11 and Server 2022/2025 x64 with MSVC v142; actual MSVC/NTFS execution is
not proven by the Linux checks. See `tests/README.md` for observed proof and limits.

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

- required version: `4` (the single accepted protocol version);
- `epoch`: 16 lowercase hexadecimal characters identifying the peer's runtime instance;
- `OCL`: mandatory runtime operclass capability;
- `OCLG`: optional subscription to the derived global view, typically for consumers such as Services.

Selector meaning:

- `?`: not READY/no policy; willing to use that neighbor as the exclusive bootstrap owner;
- `-`: policy exists but no usable candidate is currently selected;
- `<servername>`: selected source/propagator.

If a direct peer does not acknowledge HEL before timeout, UDB aborts the server link. Certain recognizable legacy request shapes that omit or misplace mandatory `OCL` are aborted immediately; other malformed HEL frames are ignored, cannot confirm the capability, and may consequently reach the timeout. These paths do not emit a DB `ERR HEL` frame. An instance epoch change for the currently selected upstream SID is treated as a fresh authority stream: it resets sequence/stream latch state and requires a full reconciliation. For any other direct peer, the change only invalidates that peer's replay/OCL instance state.

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
   - Filesystem modification times (`st_mtime`) and the `modified_at` field in `INF` are strictly diagnostic metadata intended for administrative visibility (e.g. operator `/DBQ <block>` queries).
   - `mtime` is never compared to resolve conflicting records, determine block ownership, or select which node's data survives.
   - Node clock skew, timezone differences, or artificial timestamp modifications (e.g. `touch`) have zero impact on synchronization, snapshot acceptance, or convergence.

4. **Tie-breaking and standalone boundaries**:
   - SIDs and peer identifiers never override an established authority relationship. If Node A is configured to follow Node B, Node B's state always wins, regardless of lexical SID ordering (`SID(A) > SID(B)` has no effect).
   - In policy-free deployments, the first directly connected peer confirmed during bootstrap becomes the exclusive bootstrap owner. Once `READY`, a policy-free node seals itself as a standalone authority and rejects incoming remote imports, preventing accidental state clobbering by newly linked neighbors.


## 9. Native snapshot reconciliation

Reconciliation is inventory-driven pull between eligible directly linked HEL 4/OCL
peers. It is never forwarded as a snapshot through another hop. Sender manifests,
INF and transferred rows derive from one pinned six-root generation.

### 9.1 Exact wire grammar

```text
:<peer-SID> DB <target> INF <round> <block> <sha256> <count> 0 <watermark> <root-SID> <root-epoch>
:<peer-SID> DB <target> RES <round> <block>
:<peer-SID> DB <target> BEGIN <round> <block> <txid> <sha256> <watermark>
:<peer-SID> DB <target> PUT <round> <block> <txid> <Block::path> :<typed-value>
:<peer-SID> DB <target> END <round> <block> <txid> <sha256> <watermark>
:<receiver-SID> DB <peer> ACK <round> <block> <txid> <sha256> <watermark>
```

`typed-value` is exactly `!n` (NONE), `!u<canonical uint64 decimal>` (including zero),
or `!s<canonical Base64>` (string bytes). Untagged values and INF without origin
are rejected. HEL remains 4; request and ACK forms remain unchanged. All six INF
entries must agree on root SID, epoch and watermark. The neighboring HEL epoch
identifies that peer instance, not necessarily the root mutation origin. The
reserved timestamp is emitted as zero and is not freshness authority.

### 9.2 Private durable staging

BEGIN allocates a private writer owner; copied PUT rows feed bounded external sort.
Duplicate canonical keys, schema failures, wrong ownership, count/digest mismatch,
replay, quota exhaustion or invalid sequencing cannot publish active data. END
seals and validates an isolated disk root, then durably records it as pending; only
its durable receipt permits ACK. Matching active roots use durable REUSE. A
six-root PROMOTE atomically publishes content and source lineage. One block ACK
is not whole-database READY. All retained live-tail receipts and their effects
must drain under the same current authority before READY.

Defaults per block: 10,000,000 rows, 16 GiB physical staging quota including sorting
and output, 60 s inactivity and 3600 s absolute timeout. Snapshot sending is
SendQ-aware and bounded. Disconnect/HEL restart cancels ownership without deriving
a replacement root lineage. Recovery discards abandoned pending owners only after
validating their referenced roots.

### 9.3 Logical checksums

Hashes are domain-separated SHA-256 of explicit little-endian binary rows, sorted
within canonical leaves, with position/count/weight binding. Entity partitions,
full-key radix split/collapse, leaf/branch/block domains and empty positions are
canonical: hashes do not depend on insertion order, cache size, physical page IDs
or compaction history. Empty blocks have block-specific nonempty digest inputs,
not SHA-256 of an empty byte string. ASCII keys fold except the exact K::F pattern.
The independent reference lives in `tests/support/udb_test_support/native_reference.py`.

### 9.4 Snapshot/live ordering

The import binds the exact root SID/epoch/watermark. Covered provisional entries
are discarded, and the retained tail must start at watermark+1 without gaps or
wrong origins. Copied replay envelopes reserve their future job credits within
the same 8 MiB queue limit, so a full ingress cannot deadlock its own drain.
Promotion/effects precede replay; replay/effects precede READY. No partial or stale
round can authorize READY, downstream serving or stale relay.

## 10. Live mutations and sequencing

Authorized mutations originated by the selected authoritative propagator are:

```text
INS <epoch> <seq> <Block::path> :<typed-value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
EXP <path> <expected-expires>
```

Parameters:
- `epoch`: 16 lowercase hex characters identifying the root origin's instance epoch.
- `seq`: 64-bit decimal integer strictly monotonic within the `(root source SID, epoch)` stream (`seq >= 1`), shared across all six persistent blocks (`N`, `C`, `I`, `S`, `L`, `K`) to ensure total causal ordering.

Semantics:

- `INS`: insert/replace after limits and schema validation.
- `DEL`: delete a path; deleting a missing path is idempotent.
- `DRP`: drop a complete block, publishing a durable empty root.
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
1. Every `anti-entropy-interval` (default 1800 s ± 180 s jitter), a follower sends `MANIFEST REQ` to its selected authority.
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


### 10.4 S2S examples for IRC services

This catalogue covers every persistent property of N/C/I/S/L/K and all five K
types. It is not an initial-sync implementation or a batch to deploy unchanged:
it includes operator privileges, bans and public demonstration secrets. Local
`unrealircd.conf` directives and runtime-only OCL/OCLG are separate interfaces.

Assume the connected services server is `services.example.net` (SID `9SV`) and
the directly linked IRCd is `irc.example.net` (SID `001`). Services is the root
authority for this example, with epoch `0123456789abcdef` (16 lowercase hex digits).
Its capability advertisement uses its own server name as selector. A request is:

```text
:9SV DB 001 HEL 4 services.example.net 0123456789abcdef OCL
```

When receiving the IRCd's own HEL request, services responds:

```text
:9SV DB 001 HEL 4 ACK services.example.net 0123456789abcdef OCL
```

Both directions must confirm HEL 4/OCL. HEL is not cryptographic authentication
or a grant of write authority; use authenticated TLS with certificate verification.
The receiving IRCd must already select this direct peer as its authority and have
reconciled all six blocks, adopting root `(9SV, 0123456789abcdef)` at watermark `0`.
Advertising OCL requires supporting its contract; a token alone is not a services
implementation. An unauthorized peer cannot authorize itself by writing
`S::propagator`.

Only under those assumptions do the following live mutations start at sequence 1.
For an existing stream, continue at its next sequence, never restart this example's
counter. INS/DEL/DRP share one consecutive uint64 sequence across all six blocks.
Prefix frames with the root **server SID**, not a NickServ user UID. Relays preserve
the root SID/epoch/sequence even when the selected direct transport peer differs.
Terminate every line with CRLF (`\r\n`); send no Markdown fences or comments.
Each INS is an individual mutation, not an atomic transaction containing the catalogue.
Modes/snomasks must have loaded handlers, and the example operclass `netadmin`
must exist for its intended effect. Replace server names and policies with your
network's authorized values; HEL selectors must reflect the sender's actual state.

#### N — Nicks

All nine properties are strings. The optional root entry illustrates NONE.
The `pass` below is the full `argon2id:$argon2id$…` string, Base64-encoded, for
public example password `EXAMPLE-ONLY-CHANGE-ME` and salt `udb-example-salt`.
Do not reuse these demonstration credentials. The supported `sha256:` and `crypt:`
hash strings use the same `!s` envelope; use Argon2id for new credentials.
`forbid` and `suspend` target separate demonstration nicks, not `alice`.

Decoded pass value (before Base64):

```text
argon2id:$argon2id$v=19$m=19456,t=2,p=1$dWRiLWV4YW1wbGUtc2FsdA$i5Orjj4GOgK7ORVIoiTYp+9CFRLbOifSIkDenWCrFVo
```

| Wire path | Logical value |
|---|---|
| `N::alice` | `NONE` |
| `N::alice::access` | `203.0.113.0/24` |
| `N::alice::pass` | Argon2id hash shown above |
| `N::alice::vhost` | `alice.example.net` |
| `N::Reserved::forbid` | `Reserved nick` |
| `N::Suspended::suspend` | `Suspended account` |
| `N::alice::oper` | `netadmin` |
| `N::alice::modes` | `+i` |
| `N::alice::snomasks` | `+c-k` |
| `N::alice::swhois` | `Network staff` |

```text
:9SV DB * INS 0123456789abcdef 1 N::alice :!n
:9SV DB * INS 0123456789abcdef 2 N::alice::access :!sMjAzLjAuMTEzLjAvMjQ=
:9SV DB * INS 0123456789abcdef 3 N::alice::pass :!sYXJnb24yaWQ6JGFyZ29uMmlkJHY9MTkkbT0xOTQ1Nix0PTIscD0xJGRXUmlMV1Y0WVcxd2JHVXRjMkZzZEEkaTVPcmpqNEdPZ0s3T1JWSW9pVFlwKzlDRlJMYk9pZlNJa0RlbldDckZWbw==
:9SV DB * INS 0123456789abcdef 4 N::alice::vhost :!sYWxpY2UuZXhhbXBsZS5uZXQ=
:9SV DB * INS 0123456789abcdef 5 N::Reserved::forbid :!sUmVzZXJ2ZWQgbmljaw==
:9SV DB * INS 0123456789abcdef 6 N::Suspended::suspend :!sU3VzcGVuZGVkIGFjY291bnQ=
:9SV DB * INS 0123456789abcdef 7 N::alice::oper :!sbmV0YWRtaW4=
:9SV DB * INS 0123456789abcdef 8 N::alice::modes :!sK2k=
:9SV DB * INS 0123456789abcdef 9 N::alice::snomasks :!sK2Mtaw==
:9SV DB * INS 0123456789abcdef 10 N::alice::swhois :!sTmV0d29yayBzdGFmZg==
```

#### C — Channels

The founder is `alice`, modes are `+nt` and the topic is `Hello world`.
The access container and `bob` entry illustrate NONE. The child can alternatively
carry `!u10` or `!sbWVtYmVy` (text `member`), but the JOIN hook does not interpret
its value as a rank. Forbid/suspend target separate demonstration channels.

| Wire path | Logical value |
|---|---|
| `C::#chat` | `NONE` |
| `C::#chat::founder` | `alice` |
| `C::#chat::modes` | `+nt` |
| `C::#chat::topic` | `Hello world` |
| `C::#chat::access` | `NONE` |
| `C::#chat::access::bob` | `NONE` |
| `C::#reserved::forbid` | `Reserved channel` |
| `C::#suspended::suspend` | `Suspended channel` |
| `C::#chat::options` | `63` |

```text
:9SV DB * INS 0123456789abcdef 11 C::#chat :!n
:9SV DB * INS 0123456789abcdef 12 C::#chat::founder :!sYWxpY2U=
:9SV DB * INS 0123456789abcdef 13 C::#chat::modes :!sK250
:9SV DB * INS 0123456789abcdef 14 C::#chat::topic :!sSGVsbG8gd29ybGQ=
:9SV DB * INS 0123456789abcdef 15 C::#chat::access :!n
:9SV DB * INS 0123456789abcdef 16 C::#chat::access::bob :!n
:9SV DB * INS 0123456789abcdef 17 C::#reserved::forbid :!sUmVzZXJ2ZWQgY2hhbm5lbA==
:9SV DB * INS 0123456789abcdef 18 C::#suspended::suspend :!sU3VzcGVuZGVkIGNoYW5uZWw=
:9SV DB * INS 0123456789abcdef 19 C::#chat::options :!u63
```

The example mask 63 enables all six bits; 6 combines LOCK_MODES and LOCK_TOPIC.

| Bit | Value | Wire payload alone |
|---|---:|---|
| `PROTECT_BANS` | 1 | `!u1` |
| `LOCK_MODES` | 2 | `!u2` |
| `LOCK_TOPIC` | 4 | `!u4` |
| `PERSISTENT` | 8 | `!u8` |
| `OPER_ONLY` | 16 | `!u16` |
| `SECURE_OPS` | 32 | `!u32` |

#### I — IP/realhost policies

The example allows five connections, stores all allowed `nolines` flags as the
string `GZQSTmc`, and sets visible host `shared.example.net`. This root lookup is
exact against IP or realhost, not CIDR matching.

| Wire path | Logical value |
|---|---|
| `I::203.0.113.20` | `NONE` |
| `I::203.0.113.20::clones` | `5` |
| `I::203.0.113.20::nolines` | `GZQSTmc` |
| `I::203.0.113.20::host` | `shared.example.net` |

```text
:9SV DB * INS 0123456789abcdef 20 I::203.0.113.20 :!n
:9SV DB * INS 0123456789abcdef 21 I::203.0.113.20::clones :!u5
:9SV DB * INS 0123456789abcdef 22 I::203.0.113.20::nolines :!sR1pRU1RtYw==
:9SV DB * INS 0123456789abcdef 23 I::203.0.113.20::host :!sc2hhcmVkLmV4YW1wbGUubmV0
```

#### S — Settings

The global clone limit is 3; the rejection message is `Too many connections` and
password-failure flood policy is text `5:60`. The suffix is `.users.example.net`.
Service masks below are strings `NickServ!service@services.example.net`,
`ChanServ!service@services.example.net` and `IpServ!service@services.example.net`;
they select matching connected ULine users rather than introducing those users.
The propagator value is the ordered list `services.example.net,backup.example.net`.
`encryption_key` contains `0123456789abcdef` repeated four times: Base64 encodes
those **64 hexadecimal characters**, not 32 raw key bytes. This public key is
for demonstration only; do not deploy it.

| Wire path | Logical value |
|---|---|
| `S::clones` | `3` |
| `S::quit_clones` | `Too many connections` |
| `S::flood` | `5:60` |
| `S::encryption_key` | `0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef` |
| `S::suffix` | `.users.example.net` |
| `S::nickserv` | `NickServ!service@services.example.net` |
| `S::chanserv` | `ChanServ!service@services.example.net` |
| `S::ipserv` | `IpServ!service@services.example.net` |
| `S::propagator` | `services.example.net,backup.example.net` |

```text
:9SV DB * INS 0123456789abcdef 24 S::clones :!u3
:9SV DB * INS 0123456789abcdef 25 S::quit_clones :!sVG9vIG1hbnkgY29ubmVjdGlvbnM=
:9SV DB * INS 0123456789abcdef 26 S::flood :!sNTo2MA==
:9SV DB * INS 0123456789abcdef 27 S::encryption_key :!sMDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWYwMTIzNDU2Nzg5YWJjZGVmMDEyMzQ1Njc4OWFiY2RlZg==
:9SV DB * INS 0123456789abcdef 28 S::suffix :!sLnVzZXJzLmV4YW1wbGUubmV0
:9SV DB * INS 0123456789abcdef 29 S::nickserv :!sTmlja1NlcnYhc2VydmljZUBzZXJ2aWNlcy5leGFtcGxlLm5ldA==
:9SV DB * INS 0123456789abcdef 30 S::chanserv :!sQ2hhblNlcnYhc2VydmljZUBzZXJ2aWNlcy5leGFtcGxlLm5ldA==
:9SV DB * INS 0123456789abcdef 31 S::ipserv :!sSXBTZXJ2IXNlcnZpY2VAc2VydmljZXMuZXhhbXBsZS5uZXQ=
:9SV DB * INS 0123456789abcdef 32 S::propagator :!sc2VydmljZXMuZXhhbXBsZS5uZXQsYmFja3VwLmV4YW1wbGUubmV0
```

#### L — Per-server options

`irc.example.net` is the IRCd whose local debug effect is selected. Its only
known bit is `DEBUG = 1` (`!u1`); a zero mask is `!u0`.

| Wire path | Logical value |
|---|---|
| `L::irc.example.net` | `NONE` |
| `L::irc.example.net::options` | `1` |

```text
:9SV DB * INS 0123456789abcdef 33 L::irc.example.net :!n
:9SV DB * INS 0123456789abcdef 34 L::irc.example.net::options :!u1
```

#### K — G/Z/S/Q lines and F spamfilters

Each G/Z/S/Q profile accepts `reason` and `expires`. The example's future expiry
`2000000000` is an **absolute Unix timestamp**, not a duration. For new temporary
profiles send expires before reason: a partial profile without reason is inert,
avoiding an intermediate permanent effect. Existing profiles may already be active.

The F regex is `spam[0-9]+`. Its stored component is `b64:c3BhbVswLTldKw==`, so
the wire path uses `b64%3Ac3BhbVswLTldKw==`. The decoded values are `regex`,
`cp` (channel/private messages), `gline`, 300 seconds of sanction duration,
expiry `2000000000` for the rule itself, and reason `Spam detected`. Reason is
sent last so the new rule stays incomplete until the other properties exist.
Property names are exactly **`match-type`** and **`ban-time`**, with hyphens.

| Wire path | Logical value |
|---|---|
| `K::G::*@bad.example::expires` | `2000000000` |
| `K::G::*@bad.example::reason` | `G-Line example` |
| `K::Z::2001%3Adb8%3A%3A1::expires` | `2000000000` |
| `K::Z::2001%3Adb8%3A%3A1::reason` | `Z-Line example` |
| `K::S::*@flood.example::expires` | `2000000000` |
| `K::S::*@flood.example::reason` | `Shun example` |
| `K::Q::BadNick*::expires` | `2000000000` |
| `K::Q::BadNick*::reason` | `Q-Line example` |
| `K::F::b64%3Ac3BhbVswLTldKw==::match-type` | `regex` |
| `K::F::b64%3Ac3BhbVswLTldKw==::targets` | `cp` |
| `K::F::b64%3Ac3BhbVswLTldKw==::action` | `gline` |
| `K::F::b64%3Ac3BhbVswLTldKw==::ban-time` | `300` |
| `K::F::b64%3Ac3BhbVswLTldKw==::expires` | `2000000000` |
| `K::F::b64%3Ac3BhbVswLTldKw==::reason` | `Spam detected` |

```text
:9SV DB * INS 0123456789abcdef 35 K::G::*@bad.example::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 36 K::G::*@bad.example::reason :!sRy1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 37 K::Z::2001%3Adb8%3A%3A1::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 38 K::Z::2001%3Adb8%3A%3A1::reason :!sWi1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 39 K::S::*@flood.example::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 40 K::S::*@flood.example::reason :!sU2h1biBleGFtcGxl
:9SV DB * INS 0123456789abcdef 41 K::Q::BadNick*::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 42 K::Q::BadNick*::reason :!sUS1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 43 K::F::b64%3Ac3BhbVswLTldKw==::match-type :!scmVnZXg=
:9SV DB * INS 0123456789abcdef 44 K::F::b64%3Ac3BhbVswLTldKw==::targets :!sY3A=
:9SV DB * INS 0123456789abcdef 45 K::F::b64%3Ac3BhbVswLTldKw==::action :!sZ2xpbmU=
:9SV DB * INS 0123456789abcdef 46 K::F::b64%3Ac3BhbVswLTldKw==::ban-time :!u300
:9SV DB * INS 0123456789abcdef 47 K::F::b64%3Ac3BhbVswLTldKw==::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 48 K::F::b64%3Ac3BhbVswLTldKw==::reason :!sU3BhbSBkZXRlY3RlZA==
```

The IPv6 Z path percent-encodes its colons; printable `*`, `@`, `/` and `=`
remain literal where used. The F pattern's Base64 belongs to the key and is
independent of the `!s<Base64>` encoding of property values. Pattern containers
`K::<type>::<pattern>` do **not** accept INS, including `:!n`.
No expires property means a permanent line; `expires = 0` is invalid.

#### Deletion and initial synchronization

To remove a property or a subtree, use DEL without a value; INS with NONE is
not a deletion. Continuing the example stream:

```text
:9SV DB * DEL 0123456789abcdef 49 N::Reserved::forbid
:9SV DB * DEL 0123456789abcdef 50 K::Z::2001%3Adb8%3A%3A1
```

Deleting only `expires` makes the remaining valid K profile permanent. For
initial snapshots, use the same typed payload inside PUT rather than sending
a catalogue of live INS frames immediately after HEL. For example, within an
already authorized round and begun transaction:

```text
:9SV DB 001 PUT <round> N <txid> N::alice::vhost :!sYWxpY2UuZXhhbXBsZS5uZXQ=
```

This requires the complete INF/RES/BEGIN/PUT/END/ACK lifecycle from section 9,
including exact digests and origin/watermark binding for all six blocks. A bare
PUT, successful send or PONG is not proof of durable commit or READY. Handle ERR
and recovery according to the actual stream state; do not reuse a sequence for
a different mutation. These examples are checked against the schema and codec;
they are not a claim of live integration testing with an external services daemon.

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

The initial `NICK` command applies the same readiness rejection before nickname policy lookup, including during CAP negotiation. An unnamed client receives an explicit `ERROR` and disconnect instead of silently waiting for a registration timeout; this does not bypass bootstrap or change authority selection.

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
- durability uncertainty during data/WAL flush poisons admission and revokes READY;
- incompatible HEL/OCL can close a server link rather than silently form a mixed inconsistent network.

### 15.1 Trust and transport boundary

HEL 4 authenticates neither a server nor a payload. UDB trusts UnrealIRCd's authenticated server link and then applies its own direct-peer authority policy, sequencing, staging, and schema checks. Every UDB hop should therefore use TLS with certificate verification and tightly controlled link credentials. Without TLS, the DB protocol exposes full snapshots and live values in transit, including password hashes, encryption material, and channel keys; integrity also depends on the underlying link authentication. UDB provides no end-to-end encryption or signature above the server link.

Never edit native binary files while the daemon is live. Manual changes bypass immutable generation ownership and can corrupt page integrity, schemas, logical digests or the global WAL/checkpoint authority.

## 16. Rehash and policy changes

During rehash:

- new configuration does not prematurely erase the last known-good propagator override if rehash fails;
- after successful completion, removal of `udb::propagator` removes the local override and restores normal precedence;
- policy changes are announced internally and sessions owned by an invalid source are aborted;
- identified holders keep nickname, account and effects: applied identity markers are preserved during rehash reload and is reconciled against the reloaded records, and preserved state is revoked if the reloaded database is not READY or the module fails to reload;
- the local OCL inventory is rebuilt and, when changed, a new generation is published.

Runtime `S::propagator` changes trigger equivalent authority reevaluation after the new record/block has committed.

## 17. Build and bundle

Canonical sources: `src/`. Distribution artifact: `dist/udb.c`.

```bash
scripts/format-sources.sh
python3 scripts/bundle.py
```

Deterministically regenerates `dist/udb.c` and `modules.list`.

```bash
python3 scripts/bundle.py --check
```

Read-only verification that both artifacts exactly match current sources. The generator fails if a `.c.inc` unit is orphaned or included more than once.

CI builds a supported UnrealIRCd Stable and exercises the current pytest/cmocka/native contracts. See `tests/README.md` for measured evidence and unverified boundaries; removed text/tree test suites are not proof of this engine.

## 18. DB protocol quick reference

```text
HEL 4 <selector> <epoch> OCL [OCLG]
HEL 4 ACK <selector> <epoch> OCL [OCLG]

INF <round> <block> <sha256> <record_count> 0 <watermark_seq> <root-SID> <root-epoch>
RES <round> <block>
BEGIN <round> <block> <txid> <sha256> <watermark_seq>
PUT <round> <block> <txid> <Block::path> :<typed-value>
END <round> <block> <txid> <sha256> <watermark_seq>
ACK <round> <block> <txid> <sha256> <watermark_seq>
ERR <subcmd> <code> <round/correlation> <block>

INS <epoch> <seq> <Block::path> :<typed-value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
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
8. retain the complete stopped binary store and logs for diagnosis; never edit live data.

## 20. Verified current limitations

At the documented commit:

- `C::<channel>::access::<nick>` values do not define ranks; child presence plus `+r` identification authorizes JOIN.
- `C::<channel>::modes` rejects the registration marker `r`, member ranks (`q`, `a`, `o`, `h`, `v`), `P`, and `O`; native `+r` tracks registration and `suspend` state, while `P` and `O` come only from `C::options`.
- I root keys use exact runtime lookup and are not CIDR matching rules.
- `PERSISTENT` depends on UnrealIRCd having native channel mode `+P`; UDB does not create a substitute.
- `OPER_ONLY` enforces JOIN from UDB state even when UnrealIRCd's native `+O` handler is unavailable.

These notes intentionally describe real code behavior rather than capabilities inferred from the data model.

## 21. Linux qualification evidence

Recorded on Linux x86-64, 2026-10-10 UTC. The [JSON report](native_storage_benchmarks.json) contains all nine single-sample runs and the separate point-write probe.

| Rows | Cache MiB | Recovery (s) | 10K repeated reads (ms) | Peak RSS (MiB) |
|---:|---:|---:|---:|---:|
| 1,000,000 | 128 | 2.400 | 1055 | 141.9 |
| 1,000,000 | 256 | 2.415 | 845 | 273.9 |
| 1,000,000 | 512 | 2.315 | 456 | 537.9 |
| 5,000,000 | 128 | 6.398 | 1084 | 142.2 |
| 5,000,000 | 256 | 5.900 | 878 | 273.9 |
| 5,000,000 | 512 | 5.751 | 557 | 538.1 |
| 10,000,000 | 128 | 12.597 | 1069 | 141.8 |
| 10,000,000 | 256 | 10.443 | 879 | 273.9 |
| 10,000,000 | 512 | 12.059 | 715 | 538.0 |

Rows are unique nick vhost properties, not full profiles. Bulk creation uses the
production sorter/rebuilder with a stopped, owner-serialized writer; it is not a
10M-row protocol import. Recovery executes the full referenced-data validation.
The first read pass starts with cold DATA cache but warmed indices and a retained
OS page cache; the repeat pass uses the same 10K keys. Do not interpret these
numbers as physical cold-disk latency or statistically controlled cache comparisons.

The nine bulk/read runs peaked at 2.23–2.35 MiB of charged AUX memory. Stable-file
sizes were 1.74/1.99/2.45 GiB for 1M/5M/10M, respectively, including metadata,
physical COW/catalog pages and reusable dead extents, not just live row payload.
The separate 10M/256 MiB probe performed ten real queued durable point mutations
plus origin binding in 59 ms, wrote 4,653,056 bytes through the production positioned
I/O backend and grew stable files by zero bytes through extent reuse. Its AUX peak
was 2.89 MiB. It verifies the held prior snapshot still reads the original value;
it is neither sustained write throughput nor evidence that every update has that cost.

Final new-contract checks cover typed wire, corruption, incomplete WAL tails,
EIO/ENOSPC, process exit before/partway through WAL and after WAL flush, pinned
checkpoint safety, radix split/collapse, cache/queue bounds, all-six atomic import,
rejected checksums, retained-tail replay, native effect publication, restart,
local rehash, ordered authority failover/failback and A→B→C snapshots/multihop mutations with root lineage. OCL runtime
inventory checks are separate; this is not exhaustive OCL multihop or all-topology failover proof.

The cache/AUX caps bound managed storage allocations, not total daemon memory.
Native TKL/spamfilters, persistent channels, connected users and runtime effect
ownership allocate separately in UnrealIRCd; millions of active K bans or
persistent C channels can therefore consume additional memory. The reported RSS
belongs to the storage driver with N vhost properties, not that daemon workload.

Actual MSVC v142/Windows 11 or Server 2022/2025 local NTFS execution, Windows ACL
and crash qualification, real host power loss and sustained network-import/write
load remain unverified. Linux success must not be presented as those guarantees.
