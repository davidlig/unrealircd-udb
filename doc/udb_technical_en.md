# UDB (Unreal DataBase) v4 - Technical Specification & Protocol

This documentation details the internal workings, database design, and Server-to-Server (S2S) protocol of the **UDB (Unreal DataBase) v4.0.0** module for UnrealIRCd 6.2.x.

This document is designed for developers who wish to implement clients, IRC Services, or other bots compatible with the UDB protocol.

## 1. Database Architecture

UDB uses an in-memory tree structure and flat-text file storage for persistence. The database is divided into "Blocks", identified by a letter (N, C, I, K, S, L).

### 1.1 Storage Format (Plain Text)
Disk storage (`udb_X.db`) follows a flat hierarchical structure:
```text
; UDB Block N - Version 1
; Saved: 1786942751
; Records: 5
davidlig::pass sha256:abcd1234efgh
davidlig::vhost admin.davidlig.net
davidlig::oper netadmin
```
*   Sub-levels are separated by the `::` delimiter.
*   An asterisk `*` prefix in the value indicates that the data is numeric (integer).
*   The absence of `*` indicates that the data is a text string.

### 1.2 Database Directory
`udb::database-directory` selects the directory containing every block file:
`udb_N.db`, `udb_C.db`, `udb_I.db`, `udb_S.db`, `udb_L.db`, and `udb_K.db`.
Absolute local paths are used as written. Relative paths resolve beneath
UnrealIRCd's `PERMDATADIR`. The setting rejects URLs, line breaks, and paths
that cannot safely contain a block filename. UDB creates the final directory
with mode `0700` when needed and refuses to start if it is not a directory or
cannot be created. Omitting the setting preserves the legacy location:
`PERMDATADIR` itself.

### 1.3 Supported Blocks and Their Options

#### Block N (Nicks - Users)
Stores configurations for registered users.
*   **pass**: User password hash. Only Argon2id (`$argon2id$...`), `sha256:`,
    and `crypt:` values are accepted. Plaintext, MD5, bcrypt, and unknown
    values fail closed.
*   **access**: Optional comma- or whitespace-separated IPv4/IPv6 CIDR list.
    A successful NICK or GHOST credential check must also match this list.
*   **vhost**: Custom virtual host applied on connect.
*   **oper**: IRCop class name (`operclass`, e.g., `locop`, `globop`, `admin`, `services-admin`, `netadmin`, or `-with-override` variants). Validated against local configuration via `find_operclass()`.
*   **swhois**: Extra line in the user's /WHOIS output.
*   **snomasks**: Snomasks to apply automatically.
*   **modes**: User modes to enforce upon authentication.

#### Block C (Channels)
*   **founder**: Nickname of the original channel founder (granted +q automatically).
*   **modes**: Channel modes managed by UDB. Parameters follow the mode string. Mode letters and parameter counts are strictly validated (e.g. `+ntMl` without parameters is rejected, requiring `+ntMl 50`). Deleting via `DEL` does not revert live channel modes.
*   **mlock**: Absolute channel mode lock (`*1` on `INS`; disabled via `DEL`). When enabled (`*1`), nobody can modify channel modes via `MODE` or `SAMODE`.
*   **topiclock**: Absolute channel topic lock (`*1` on `INS`; disabled via `DEL`). When enabled (`*1`), nobody can modify the channel topic via `TOPIC`.
*   **topic**: The persistent channel topic.
*   **access**: Child records keyed by the identified nicknames allowed to join.
*   **forbid**: Channel prohibition reason.
*   **suspended**: Disables registered-channel founder and `+r` behavior.
*   **pass** and **challenge**: Channel-admin authentication credential.
*   **persistent**: Sets native `+P` when the permanent-channel mode handler is loaded (`*1` on `INS`; disabled and removed via `DEL`). If the channel does not exist on insert, it is created; if empty on `DEL`, it is destroyed.
*   **options**: Numeric channel options: `*1` protects locally-added bans,
    and `*2` locks every local `MODE` and `SAMODE` change to the identified
    founder.

### 1.3 Live Channel Reconciliation

UDB owns the founder `+q` state of a registered channel. When `founder` is
replaced, UDB removes `+q` from the previous present founder and grants it to
the new identified founder. A founder never receives `+o` from UDB.

`pass` and `challenge` authenticate a joining user for that channel and grant
only `+a` for the current membership. They are not a source of `+o`. Replacing
or deleting either credential revokes `+a` only when it was granted by UDB.
Deleting the channel profile revokes UDB-managed founder and channel-admin
privileges and clears its persistent topic.

`INVITE <nick> <channel> <password>` validates `C::<#channel>::pass` before
performing the native invite. A successful local invite gives its local target a
one-use entry grant that expires after five minutes. It bypasses only UDB's
password check and never grants `+a`; a password supplied directly to `JOIN`
continues to grant `+a`. Password-bearing INVITEs to remote targets are rejected
because the one-use grant is intentionally local and is not sent over S2S.

All UDB credential checks accept only `challenge` values `argon2id`, `sha256`,
and `crypt`. Plaintext, MD5, bcrypt, and unsupported challenge names fail
closed. Failed checks are bounded and rate-limited by the active `S::flood` or
`udb::password-flood` setting per profile and source IP.

UDB channel effects are executed with the connected ChanServ client resolved
from `S::chanserv`: persistent topic updates and removals, configured channel
modes, and UDB-managed member ranks (`q`, `a`, `o`, `h`, and `v`). This preserves
the native MODE/TOPIC protocol while making the service client the visible
origin. A missing or ambiguous service client falls back to the local server
source and is logged; UDB never fabricates a client.

UDB uses command overrides for `MODE` and `SAMODE`, rather than raw command
text inspection. With channel option `*1`, locally-added `+b` masks are tracked
with their setter and cannot be removed by another local user, except an
identified founder or an oper. Option `*2` rejects any local mode change by a
non-founder; it is not limited to the modes stored in `C::<#channel>::modes`.

#### Block I (IPs and Hosts)
*   **clones**: Numeric limit of simultaneous connections (`*<number>`).
*   **host**: Explicit host override for matching local clients. UDB preserves
    the original real host, cloak host, virtual host, and host modes, then
    restores them when the record is replaced, deleted, or UDB unloads.
*   **nolines**: Ban-exception type letters passed to UnrealIRCd (for example,
    `GZQSTmc`). UDB removes only exceptions it created. `c` additionally
    exempts the matching IP/host from UDB's clone throttle.

#### Block K (Lines and Bans)
Defines active network sanctions.
*   **G**: G-Line (Global user@host ban).
*   **Z**: Z-Line (Global IP ban).
*   **S**: Shun (Global communication ban).
*   **Q**: Q-Line (Nick ban).
*   **F**: Spamfilter (Ban by regular expressions).
    *   *Internal options for F:* `type` (target), `action`, `duration`, `reason`.

Line records use `K::<type>::<pattern>` with optional child properties, for
example `K::G::*@bad.example::duration *3600` and
`K::G::*@bad.example::reason abuse`. `duration` is expressed in seconds and
expires the G, Z, S, Q, or F record; `0` (or no duration record) is permanent.
For F, the same duration is also passed to the spamfilter action TKL.

Spamfilter patterns are plain regex strings by default for compatibility. To
store a pattern that must be represented safely as base64, prefix standard,
padded RFC 4648 base64 with `b64:`: `K::F::b64:Zm9vL2Jhcg==::type c` decodes to
`foo/bar`. The payload must be a non-empty, valid padded base64 value whose
decoded pattern is at most 3072 bytes and contains no NUL bytes. Invalid
encoded patterns are rejected and never compiled or installed.

#### Block S (Global / Setup)
Global network settings and UDB behavior.
Only these values are supported: `quit_ips`, `quit_clones`, `flood`,
`encryption_key`, `suffix`, `nickserv`, `chanserv`, and `ipserv`.
*   **flood**: `<attempts>:<seconds>` password-failure limit. It overrides
    `udb::password-flood`; deleting it restores that configured value.
*   **encryption_key** and **suffix**: A 64-hex-character HMAC key and a valid
    dotted suffix (starting with `.`) enable deterministic derived vhosts.
    UDB HMAC-SHA-256s `UDB-vhost-v1|<original-ip>|<original-host>`, uses the
    first 16 bytes as 32 lowercase hex characters, and appends the suffix.
    Both records are required; changing or deleting either reconciles connected
    local clients. `N::<nick>::vhost` and explicit `I::<ip>::host` take
    precedence over a derived vhost.
*   **nickserv**, **chanserv**, **ipserv**: Service masks in `nick!user@host`
    form. Each mask is resolved dynamically against exactly one connected,
    non-dead ULine user with UnrealIRCd's native user-mask matcher. The result
    is not cached. If there is no unique match, the affected event uses the
    local server source and UDB logs the safe fallback.
    NickServ is the visible source of nick-related notices, including invalid
    password and password-flood notifications. IpServ is the visible source
    of explicit nick vhost, derived IP vhost, and `I::<ip>::host` change or
    restoration notices.
*   **quit_ips** and **quit_clones**: Validated disconnect-message state; the
    clone hook consumes `quit_clones`.

#### Block L (S2S Links)
Only `L::<server>::options` is supported. Its numeric bitmask is `*1` for UDB
debug notices and `*2` for the propagator source. Select exactly one source:
either `udb::propagator` or one `L` record with `*2`; zero or multiple sources
reject remote writes. `prefix` and `allow_clients` are not supported settings.

---

## 2. S2S (Server-to-Server) Protocol

The UDB protocol integrates into UnrealIRCd's native S2S traffic using the extended `DB` command.
Only directly linked peers that explicitly complete the UDB HEL exchange have
the UDB V4 protocol capability. Capability does not authorize data access:
staged `BEGIN`, `PUT`, and `END` imports, and `RES` requests and exports, are
accepted only on the direct peer selected as the configured propagator. This
permits edge-local A-to-B-to-C propagation when B selects A and C selects B.
Real-time mutations must likewise originate from the configured
`udb::propagator`; a peer that is actively serving an authorized block
synchronization may only send records for that block.
**General structure:**
`:<source_sid> DB <target> <subcommand> <parameters>`

### 2.1 Initial Synchronization (Handshake)
When a server connects to another, block states are verified using a CRC32 over
the canonical logical records. The digest sorts serialized `path value` records,
so save timestamps, comment headers, and sibling insertion order do not affect
it. After `HOOKTYPE_SERVER_SYNC`, each directly linked peer receives one
`HEL 4 <selected-propagator>` request. Only the matching direct `HEL 4 ACK` confirms UDB V4 for that
link; no `INF`, staged frame, or forwarded UDB DB frame is sent first. A missing
acknowledgement times out after 60 seconds and marks that link unsupported until
it reconnects. `HEL` is the only DB frame accepted before confirmation and is
never routed beyond the direct link.

**HEL (Capability Negotiation):**
`:<sid> DB <direct-peer-sid> HEL 4 <selected-propagator>`

The selected propagator field is `-` when no unique source is configured. It
lets the direct peer authorize outbound snapshots only when it is explicitly
selected by the receiver.

**HEL acknowledgement:**
`:<sid> DB <direct-peer-sid> HEL 4 ACK`

**INF (Block Information):**
`:<sid> DB <target> INF <block_letter> <crc32_hex> <timestamp>`

**RES (Sync Request):**
`:<sid> DB <target> RES <block_letter>`

When checksums differ, the newer `timestamp` wins. If timestamps are equal,
the lexicographically higher server SID wins. The SID is the immutable server
identity already carried in the DB frame, unlike a configurable server name or
link arrival order. Only the loser sends `RES` for each block; this prevents reciprocal RES and
snapshot exchanges while retaining the configured direct-propagator checks.

For peers with the staged capability, `RES` is answered with a transaction:

**BEGIN:** `:<sid> DB <target> BEGIN <block> <txid> <digest>`

**PUT:** `:<sid> DB <target> PUT <block> <txid> <path> :<value>`

**END:** `:<sid> DB <target> END <block> <txid> <digest>`

**ACK:** `:<sid> DB <target> ACK <block> <txid> <digest>`

The requester and receiver of this transaction must be the selected direct
propagator. A HEL-confirmed peer that is not selected receives
`UDB_ERR_FORBIDDEN` for `RES`, `BEGIN`, `PUT`, and `END`; it cannot create or
continue a staged session, trigger a block export, or cause those frames to be
forwarded.

`PUT` paths omit the block prefix because the block is an explicit parameter.
Persisted records must have non-empty `::` path components and fit the record
path limit. UDB logs and skips malformed or overlong persisted lines while
continuing to load the remaining records in that block.
The receiver builds an isolated tree per block and never applies its runtime
effects during transfer. On `END`, it verifies the canonical digest, writes the
staged tree atomically via the block temporary file, removes every runtime
effect represented by the outgoing tree, then replaces the active tree and
recursively applies each incoming effect owner once. This includes live N, C,
I, S/L, and K state, so nested K patterns are installed after commit without
duplicate application. A peer quit, 60-second inactivity
timeout, malformed `PUT`, unexpected transaction ID, or bad digest discards the
staged tree only. The prior active and durable tree remain in use.
An `END` digest is valid only when its entire non-empty field is hexadecimal and
fits in `unsigned long`; partial input and overflow are rejected even when an
empty staged tree has digest zero.

While a block has a staged transaction, real-time `INS`, `DEL`, `DRP`, and `OPT`
are rejected with `UDB_ERR_SYNC_ACTIVE`, including requests from the propagator.
Outside a transaction those mutations still require the configured propagator.

`FDR` is not emitted by the HEL 4 protocol and is not part of staged transfer.

### 2.2 Real-time Data Modification
To inject or delete records on the fly, the following commands are used (usually with target `*` for broadcast).

**INS (Insert / Modify):**
`:<sid> DB * INS <block_letter>::<key>[::<subkey>] <value>`
*Example:* `:<sid> DB * INS N::davidlig::vhost admin.davidlig.net`

**DEL (Delete):**
Deletes a node and cascades to children.
`:<sid> DB * DEL <block_letter>::<key>[::<subkey>]`
*Example:* `:<sid> DB * DEL C::#opers::topic`

After HEL 4 confirmation, real-time `INS` and `DEL` are accepted only from the
selected propagator, except frames belonging to the peer currently serving that
block's synchronization. They are rejected while that block has a staged
transaction and are persisted and forwarded only to HEL-confirmed direct peers.
For `INS`, `DEL`, and `DRP`, UDB first clones the active block and applies the
change to that private candidate. It atomically writes and renames the candidate
snapshot before changing active indexes, counters, or runtime effects. Replacing
an existing `INS` record revokes its old runtime effects before applying the
candidate, including an `N::<nick>::oper` downgrade. An `INS` whose value equals
the stored value is idempotent: it is persisted without revoking or re-applying
effects, so re-sending `C::<#channel>::modes` with the same value neither churns
channel modes nor revokes the founder `+q`. Replacing a channel profile
(`C::<#channel>`) restores the revoked effects from the surviving profile:
founder, modes, `+P`, and topic. `OPT` likewise writes its
snapshot before updating metadata or forwarding. A write failure leaves active
state and the durable file unchanged, returns `ERR`, and does not forward the
mutation.

Snapshots are created with exclusive creation and mode `0600`, independent of
the process umask. Where the platform provides `O_NOFOLLOW`, it is used as an
additional symlink safeguard. UDB flushes and `fsync`s the temporary snapshot
before closing and renaming it, then `fsync`s the containing directory after the
rename. UDB aborts and removes its temporary snapshot on open, permission,
stream, file-sync, close, or rename failure. A directory-sync failure is
reported after the replacement is visible, but before its crash durability is
confirmed; snapshots and active block files always remain beneath the configured
database directory.

**DRP (Drop / Empty Block):**
`:<sid> DB * DRP <block_letter>`

**OPT (Optimize):**
`:<sid> DB * OPT <block_letter>`

### 2.3 Error Handling (ERR)
`:<sid> DB <target> ERR <subcommand> <error_code> <extra>`
*   `1`: UDB_ERR_NO_BLOCK (Specified block does not exist)
*   `7`: UDB_ERR_SYNC_ACTIVE (A synchronization is already in progress)
*   `8`: UDB_ERR_NO_SYNC (No synchronization was requested)
*   `9`: UDB_ERR_FORBIDDEN (Action denied due to permissions)

### 2.4 DBQ Secret Redaction

`DBQ` requires oper privileges and never returns the value of `pass`,
`challenge`, or `encryption_key`. Direct queries and child listings show
`<redacted>` for those records.

---

## 3. Credits and License

**Original Author (UnrealIRCd 3.x):**
The UDB protocol and its classic original version were conceived and developed by **Trocotronic** (*www.redyc.com*). The current module and protocol optimizations are developed and maintained under the project URL **https://github.com/davidlig/unrealircd-udb** by **David Abuín Fontán ('davidlig')**.

**Current Version (UnrealIRCd 6.2.x - UDB v4.0.0):**
Modern refactoring, transition to the new modular C architecture, and full adaptation to the new UnrealIRCd v6 API developed by **David Abuín Fontán "davidlig"** (2026).
UDB has been successfully converted into a standard and optimized 3rd-party module (`udb.so`), with native integration to the TKL engine, modern cryptographic security, and full support for v6 S2S routing.
