# UDB 4 (Unreal DataBase) - Technical Specification & Protocol

This documentation details the internal workings, database design, and Server-to-Server (S2S) protocol of the **UDB 4 (Unreal DataBase v4.0.0)** module for UnrealIRCd 6.2.x.

This document is designed for developers who wish to implement clients, IRC Services, or other bots compatible with the UDB protocol.

## 1. Database Architecture

UDB uses an in-memory tree structure and flat-text file storage for persistence. The database is divided into "Blocks", identified by a letter (N, C, I, K, S, L).

### 1.1 Storage Format (Plain Text) & Path Encoding
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
*   To support IPv6 addresses (e.g. `2001:db8::1`) and special characters without ambiguity, individual path components are canonically percent-encoded (`%XX`, e.g. `2001%3Adb8%3A%3A1`) in disk files, S2S frames, and HMAC checksum digests. In memory, UDB transparently decodes path components to enable native IP matching and lookups.
*   An asterisk `*` prefix in the value indicates that the data is numeric (64-bit integer with strict format validation).
*   The absence of `*` indicates that the data is a text string.

### 1.2 Database Directory, Configuration & Transactional Loader

#### Directives in the `udb { }` block:
*   `database-directory "<path>";`: Selects the directory containing all block files
    (`udb_N.db`, `udb_C.db`, `udb_I.db`, `udb_S.db`, `udb_L.db`, `udb_K.db`).
    Absolute local paths are used as written. Relative paths resolve beneath `PERMDATADIR`.
    UDB creates the directory with `0700` permissions. Defaults to `PERMDATADIR`.
*   `propagator "<server>";`: Server authorized to propagate database syncs and live mutations.
*   `max-staged-records <number>;`: Maximum number of records allowed per block during a single
    staged sync transaction. Protects against DoS attacks and memory exhaustion (OOM).
    Allowed range: `1000` to `10000000` (default `500000`).
*   `max-global-clones <number>;`: Global limit of connections/clones per IP.
*   `password-flood <attempts>:<seconds>;`: Password brute-force flood protection (default `5:60`).

**Transactional Loader:** During startup, UDB reads each block file into a staging
candidate tree. The active in-memory database is swapped atomically only after the
file is completely read and validated without errors. If a block file does not
exist (`ENOENT`), UDB initializes cleanly with an empty database. If a file cannot
be opened or read due to permission errors (`EACCES`) or disk I/O errors, UDB aborts
module startup (`MOD_FAILED`) and marks the block with `UDB_LOAD_FAILED`, strictly
preventing the module from overwriting or destroying existing database files on disk.

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
*   **topic**: The persistent channel topic.
*   **access**: Child records keyed by the identified nicknames allowed to join.
*   **forbid**: Channel prohibition reason.
*   **suspended**: Disables registered-channel founder and `+r` behavior.
*   **pass** and **challenge**: Channel-admin authentication credential.
*   **options**: Numeric bitmask options (`*<value>`):
    *   `*1` (`0x1` / `UDB_CHOPT_PROTECT_BANS`): Protects locally-added bans (only ban author can remove their bans).
    *   `*2` (`0x2` / `UDB_CHOPT_LOCK_MODES`): Absolute channel mode lock (nobody can modify modes via `MODE` or `SAMODE`).
    *   `*4` (`0x4` / `UDB_CHOPT_LOCK_TOPIC`): Absolute channel topic lock (nobody can modify topic via `TOPIC`).
    *   `*8` (`0x8` / `UDB_CHOPT_PERSISTENT`): Sets native `+P` when the permanent-channel mode handler is loaded. If the channel does not exist on insert, it is created; if empty when disabled or removed via `DEL`, it is destroyed.
    *   Supports any combination of flags (e.g. `*6` for `0x2 | 0x4` = lock modes + lock topic, `*14` for `0x2 | 0x4 | 0x8`, etc.).

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

UDB uses command overrides for `MODE`, `SAMODE`, and `TOPIC`, rather than raw command
text inspection. With channel option `*1` (`UDB_CHOPT_PROTECT_BANS`), locally-added `+b` masks are tracked
with their setter and cannot be removed by another local user, except an
identified founder or an oper.
Option `*2` (`UDB_CHOPT_LOCK_MODES`) absolutely blocks any local mode change (including for the founder).
Option `*4` (`UDB_CHOPT_LOCK_TOPIC`) absolutely blocks any local topic change via `TOPIC` (including for the founder).
Option `*8` (`UDB_CHOPT_PERSISTENT`) maintains the channel permanently via native `+P`.

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
Only these values are supported: `clones`, `quit_ips`, `quit_clones`, `flood`,
`encryption_key`, `suffix`, `nickserv`, `chanserv`, `ipserv`, and `propagator`.
*   **clones**: Numeric global clone limit default (`*<number>`), applied when
    no IP-specific limit is set.
*   **quit_clones**: Disconnect message used by the clone throttle hook.
*   **quit_ips**: Disconnect message used by modular IP-limit subsystems.
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
*   **propagator**: Cluster authoritative propagator or priority list of authorized servers (e.g. `S::propagator "services.yourdomain.net,hub1.yourdomain.net"`).

##### Propagator Resolution Hierarchy
UDB resolves the active cluster propagator using a deterministic, fault-tolerant priority model:
1. **Priority 1 (Local Override):** Explicit `udb { propagator "<server>"; }` in `unrealircd.conf` takes strict precedence.
2. **Priority 2 (Dynamic Priority List in DB):** If `S::propagator "pri,sec"` is defined, the first server currently connected and online (`FindServer`) is elected, enabling automated, zero-latency Failover and Failback.
3. **Auto-Bootstrap (Clean Node Mode):** Nodes without local configuration accept initial staged syncs from their direct HEL 4 link partner via `HEL 4 ?` and learn the cluster authority dynamically from block `S`.

#### Block L (S2S Links)
Only `L::<server>::options` is supported. Its numeric bitmask is `*1` for UDB
debug notices.

---

## 2. S2S (Server-to-Server) Protocol

The UDB protocol integrates into UnrealIRCd's native S2S traffic using the extended `DB` command.
Only directly linked peers that explicitly complete the UDB HEL exchange have
the UDB V4 protocol capability. Capability does not authorize data access:
staged `BEGIN`, `PUT`, and `END` imports, and `RES` requests and exports, are
accepted from the direct peer selected as propagator or during auto-bootstrap of a fresh node. This
permits edge-local A-to-B-to-C propagation when B selects A and C selects B, as well as Ingest Gateway topologies where a Hub shields Services from the rest of the network.
Real-time mutations must likewise originate from the configured authority; a peer that is actively serving an authorized block
synchronization may only send records for that block.

To guarantee network-wide data integrity and consistency, configuring
`require module { name "third/udb"; };` in `unrealircd.conf` is strongly recommended.
This ensures that the server immediately aborts (`SQUIT`) any link attempt from
a node lacking UDB during the initial `SMOD` handshake.

**General structure:**
`:<source_sid> DB <target> <subcommand> <parameters>`

### 2.1 Initial Synchronization (Handshake)
When a server connects to another, block states are verified using a CRC32 over
the canonical logical records. The digest sorts serialized `path value` records,
so save timestamps, comment headers, and sibling insertion order do not affect
it. After `HOOKTYPE_SERVER_SYNC`, each directly linked peer receives one
`HEL 4 <selected-propagator>` request. Only the matching direct `HEL 4 ACK` confirms UDB V4 for that
link; no `INF`, staged frame, or forwarded UDB DB frame is sent first. A missing
acknowledgement times out after 60 seconds and automatically aborts the link with `SQUIT`.
`HEL` is the only DB frame accepted before confirmation and is
never routed beyond the direct link.

**HEL (Capability Negotiation and Auto-Bootstrap):**
`:<sid> DB <direct-peer-sid> HEL 4 <selected-propagator>`

The selected propagator field is `?` when no local source is configured. It
allows a clean node to discover cluster authority and authorizes the direct peer to supply the initial staged snapshot.

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
Persisted records must have non-empty `::` path components and fit within
`UDB_RECORD_LINE_MAX` (12320 bytes) and `UDB_RECORD_PATH_MAX` (8192 bytes). If any
malformed, overlong, or schema-invalid line is encountered during local `.db` file
loading, UDB aborts loading fail-closed with `UDB_LOAD_FAILED`, discards the candidate
tree, and logs a fatal error, preventing corruption or partial startup.
The receiver builds an isolated tree per block and never applies its runtime
effects during transfer. On `END`, it verifies the canonical digest, writes the
staged tree atomically via the block temporary file, removes every runtime
effect represented by the outgoing tree, then replaces the active tree and
recursively applies each incoming effect owner once. This includes live N, C,
I, S/L, and K state, so nested K patterns are installed after commit without
duplicate application. A peer quit, configured inactivity timeout (default 60s),
configured absolute timeout (default 300s), malformed `PUT`, staged record/byte
limit exhaustion, unexpected transaction ID, or bad digest discards the staged
tree only. The prior active and durable tree remain in use.
An `END` digest is valid only when its entire non-empty field is hexadecimal and
fits in `unsigned long`; partial input and overflow are rejected even when an
empty staged tree has digest zero.

While a block has a staged transaction, real-time `INS`, `DEL`, `DRP`, and `OPT`
are rejected with `UDB_ERR_SYNC_ACTIVE`, including requests from the propagator.
Outside a transaction those mutations still require the configured propagator.

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

UDB strictly validates all records received via `INS`, `PUT`, or loaded from disk
against a declarative per-block schema catalogue. Unknown keys, invalid hierarchy
nesting (such as composite paths in Block S), or incompatible data types are
immediately rejected with `ERR INS 2 <block>` or `ERR PUT 2 <block>` (`UDB_ERR_PARAMS`),
and cause local `.db` file parsing to abort fail-closed.

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
*   `2`: UDB_ERR_PARAMS (Missing or invalid command parameters)
*   `3`: UDB_ERR_FATAL (Fatal internal or persistence error)
*   `4`: UDB_ERR_SYNC_ACTIVE (A synchronization is already in progress)
*   `5`: UDB_ERR_NO_SYNC (No synchronization was requested)
*   `6`: UDB_ERR_FORBIDDEN (Action denied due to permissions / non-propagator)

### 2.4 DBQ Secret Redaction

`DBQ` requires oper privileges and never returns the value of `pass`,
`challenge`, or `encryption_key`. Direct queries and child listings show
`<redacted>` for those records.

---

## 3. Credits and License

**Author & Lead Developer:**
The UDB module, its modern modular architecture for UnrealIRCd 6, and the v4 protocol extensions are developed and maintained by **David Abuín Fontán ('davidlig')** (<https://github.com/davidlig/unrealircd-udb>).

**Original Concept & Idea:**
Based on the original UDB protocol concept conceived by **Trocotronic** (*www.redyc.com*).
