# UDB 4 (Unreal DataBase) - Technical Specification & Protocol

This documentation details the internal workings, database design, and Server-to-Server (S2S) protocol of the **UDB 4 (Unreal DataBase v4.1.0)** module for UnrealIRCd 6.2.x.

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
*   `propagator "<server>";`: Strict local propagator override. The value is one
    UnrealIRCd server name, must fit `HOSTLEN`, and is rejected during
    `CONFIGTEST` if it contains whitespace or CR/LF or fails UnrealIRCd's native
    server-name validation. A remote value authorizes staged sync only when it
    is a directly connected, `HEL 4`-confirmed peer.
*   `max-staged-records <number>;`: Maximum number of records allowed per block during a single
    staged sync transaction. Protects against DoS attacks and memory exhaustion (OOM).
    Allowed range: `1` to `10000000` (default `500000`).
*   `max-global-clones <number>;`: Global limit of connections/clones per IP.
*   `password-flood <attempts>:<seconds>;`: Password brute-force flood protection (default `5:60`).
*   `stale-timeout <seconds>;`: Grace period before transitioning from `DEGRADED` to `STALE` when a policy is configured or present from block S but no eligible propagator candidate is available. Allowed range: `1` to `604800` seconds (default `300`).
*   `stale-action <warn | deny-new-clients>;`: Operational mitigation policy applied upon entering `STALE` status. `deny-new-clients` (default) cleanly rejects new local user connections before registration (`HOOKTYPE_PRE_LOCAL_CONNECT`) while leaving existing clients and S2S links fully operational; `warn` emits diagnostic warning logs without restricting new client connections.

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
*   **propagator**: Ordered cluster priority list (for example,
    `S::propagator "services.example.net, hub1.example.net"`). Spaces are
    accepted only around commas and are trimmed per token. Empty tokens, tabs,
    CR/LF, names longer than `HOSTLEN`, names rejected by UnrealIRCd, and total
    values longer than `UDB_RECORD_VALUE_MAX` are rejected. Lists longer than
    512 bytes remain valid when they fit the record limit; no token is truncated.

##### Propagator Resolution Hierarchy
UDB resolves authority independently on every node; staged sync is never routed
as a multi-hop transaction:
1. **Local override:** `udb { propagator "<server>"; }` takes strict precedence.
   The local server may name itself. A remote name is eligible only if it is a
   directly connected server peer.
2. **Persisted priority list:** `S::propagator "pri,sec"` selects the first
   entry that names the local server or a directly connected server peer. A
   globally visible server reached through another hub is skipped.
3. **HEL authorization:** A selected remote peer becomes usable for staged
   `BEGIN / PUT / END / RES` only after the direct link confirms `HEL 4` and the
   peer's advertised selection authorizes the transfer.
4. **Auto-bootstrap:** A node with neither a local override nor a valid persisted
   list advertises `HEL 4 ?`, accepts its initial staged snapshot from that
   direct peer, and learns `S::propagator` from block `S`.

Selection is recomputed when links or propagator settings change. Ordered
availability therefore produces deterministic failover and failback without
retaining dead `Client *` state.

#### Block L (S2S Links)
Only `L::<server>::options` is supported. Its numeric bitmask is `*1` for UDB
debug notices.

---

## 2. S2S (Server-to-Server) Protocol

The UDB protocol integrates into UnrealIRCd's native S2S traffic using the extended `DB` command.
Only directly linked peers that explicitly complete the UDB HEL exchange have
the UDB V4 protocol capability. Capability does not authorize data access:
staged `BEGIN`, `PUT`, and `END` imports, and `RES` requests and exports, are
accepted only along the strict authority direction: a fresh node without any
propagator policy may be fed exclusively by its single bootstrap peer, and a
configured node accepts staged data only from the direct peer it selected as
propagator. Once READY without a policy, a node is its own standalone
authority and accepts no remote imports. This
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

The selected propagator field is `?` only when neither propagator source is
configured and the node is not yet READY; it allows that node to discover
cluster authority and authorizes
the single exclusive bootstrap peer to supply the initial staged snapshot. A node
that is READY without any policy is a standalone authority and advertises its
own name instead of `?`. A configured but
unavailable policy is advertised as `HEL 4 -`, not converted to `?`; `-` grants
no staged-sync authorization and therefore cannot silently broaden access.

**HEL acknowledgement:**
`:<sid> DB <direct-peer-sid> HEL 4 ACK`

**INF (Block Information):**
`:<sid> DB <target> INF <round_id> <block_letter> <crc32_hex> <timestamp>`

**RES (Sync Request):**
`:<sid> DB <target> RES <round_id> <block_letter>`

The `INF` timestamp is informational metadata only (logging, diagnostics, and
`/UDB STATUS`); it never decides authority. When checksums differ, the
receiver always requests the block from its selected authority with `RES`:
authority is the configured direct propagator (or the exclusive bootstrap peer
before READY), never a timestamp or SID comparison. Only the follower sends
`RES`; an authority never pulls from its followers, which prevents reciprocal
RES and snapshot exchange loops.

For peers with the staged capability, `RES` is answered with a transaction:

**BEGIN:** `:<sid> DB <target> BEGIN <round_id> <block> <txid> <digest>`

**PUT:** `:<sid> DB <target> PUT <round_id> <block> <txid> <path> :<value>`

**END:** `:<sid> DB <target> END <round_id> <block> <txid> <digest>`

**ACK:** `:<sid> DB <target> ACK <round_id> <block> <txid> <digest>`

The receiver accepts `BEGIN` only when it previously emitted `RES` for the same
direct peer, block, and active round. Late `INF`, `BEGIN`, `PUT`, or `END` from
another round cannot advance the current round. The requester and receiver of
this transaction must be the selected direct
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
against a declarative per-block schema catalogue and strict numeric limits. Unknown keys,
invalid hierarchy nesting (such as composite paths in Block S), or incompatible data types are
immediately rejected with `ERR INS 2 <correlation_id> <block>` or `ERR PUT 2 <round_id> <block>` (`UDB_ERR_PARAMS`),
and cause local `.db` file parsing to abort fail-closed (discarding candidate changes and leaving
the database uncorrupted).

### 2.2 Numeric Limits & Mathematical Hierarchy

To guarantee zero truncation across the entire lifecycle (runtime store, disk serialization, and Server-to-Server propagation), UDB strictly enforces a unified numeric hierarchy:

| Parameter | Limit (bytes) | Constant | Description & Mathematical Invariants |
|---|---|---|---|
| Max Path Length | 8,192 | `UDB_RECORD_PATH_MAX` | Max full path length (`block::k1::...::kN`) |
| Max Raw Component | 4,608 | `UDB_COMPONENT_RAW_MAX` | Max raw decoded component (e.g. `b64:` + 4096B regex = 4100B) |
| Max Encoded Component | 4,608 | `UDB_COMPONENT_ENCODED_MAX` | Max percent-encoded component (e.g. `b64%3A` + 4096B = 4102B) |
| Max Value Length | 4,096 | `UDB_RECORD_VALUE_MAX` | Max data payload (e.g. topic, vhost, reason, key) |
| Max Line Length | 12,320 | `UDB_RECORD_LINE_MAX` | `PATH_MAX (8192) + VALUE_MAX (4096) + 32` overhead |
| Max S2S Frame | 16,384 | `UDB_S2S_LINE_MAX` | `MAXLINELENGTH` (UnrealIRCd 6 BIGLINES frame limit) |
| S2S Overhead Buffer | 256 | `UDB_S2S_OVERHEAD_MAX` | Header space for `:SID DB SID CMD ...` |
| Max Spamfilter Regex | 3,072 | `UDB_SPAMFILTER_PATTERN_MAX` | Max raw regex pattern length |

#### Mathematical Proof for Spamfilter Encoding:
- Raw regex pattern $\le 3072$ bytes (`UDB_SPAMFILTER_PATTERN_MAX`).
- RFC 4648 Base64 encoding: $\lceil 3072 / 3 \rceil \times 4 = 4096$ characters.
- Prefixed raw component (`b64:`): $4 + 4096 = 4100$ bytes ($\le \text{UDB\_COMPONENT\_RAW\_MAX } 4608$).
- Percent-encoded component (`b64%3A`): $4100 + 2 = 4102$ bytes ($\le \text{UDB\_COMPONENT\_ENCODED\_MAX } 4608$).
- Full path (`K::F::b64%3A...::reason`): $4116$ bytes ($\le \text{UDB\_RECORD\_PATH\_MAX } 8192$).
- Serialized disk record line: $4116 + 1 + 4096 + 1 = 8214$ bytes ($\le \text{UDB\_RECORD\_LINE\_MAX } 12320$).
- S2S wire frame: $8214 + 256 = 8470$ bytes ($\le \text{UDB\_S2S\_LINE\_MAX } 16384$).

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
stream, file-sync, close, or rename failure. A successful rename is the
irreversible point: if the following directory `fsync` fails, UDB keeps the
visible snapshot as active state, writes `.udb_state` as `BOOTSTRAPPING`, returns
a persistence error, and neither acknowledges the round nor permits `READY`.
If the same failure follows a visible `.udb_state` rename to `READY`, UDB keeps
`udb_ready=0` and replaces the visible marker with `BOOTSTRAPPING`; this is
classified as a durability-uncertain commit, not as a pre-commit failure.

**DRP (Drop / Empty Block):**
`:<sid> DB * DRP <block_letter>`

**OPT (Optimize):**
`:<sid> DB * OPT <block_letter>`

### 2.3 Error Handling (ERR)
All errors use only:
`:<sid> DB <target> ERR <subcommand> <error_code> <round_id> <block>`

`round_id` is a strict non-zero decimal value. An ERR clears pending/session
state or aborts reconciliation only when its command is reconciliation-related
and the direct authority peer, block, and round match the active state; stale
errors are ignored. Real-time mutation errors use a non-zero sender-local
correlation ID and never alter reconciliation.
*   `1`: UDB_ERR_NO_BLOCK (Specified block does not exist)
*   `2`: UDB_ERR_PARAMS (Missing or invalid command parameters)
*   `3`: UDB_ERR_FATAL (Fatal internal or persistence error)
*   `4`: UDB_ERR_SYNC_ACTIVE (A synchronization is already in progress)
*   `5`: UDB_ERR_NO_SYNC (No synchronization was requested)
*   `6`: UDB_ERR_FORBIDDEN (Action denied due to permissions / non-propagator)

### 2.4 Operational Readiness, Persistence & Health (Invariants R1 - R10)

UDB enforces deterministic invariants for database readiness, durable persistence, round-isolated reconciliation, and hop-by-hop topology:

1. **Durable Database Readiness (Invariant R1):** `udb_ready=1` is reachable only after all 6 blocks (`N, C, I, S, L, K`) are durably persisted to disk, `.udb_state` is atomically persisted to `STATE=READY` (with directory `fsync`), and then `udb_ready=1` is set.
2. **Missing Snapshot Check on Restart (Invariant R2):** On restart with `.udb_state` indicating `READY`, if any required block snapshot `udb_X.db` is missing (`ENOENT` / `UDB_LOAD_EMPTY`), the node does NOT start `READY`; it logs `UDB_READY_MISSING_BLOCK` and fails closed to `BOOTSTRAPPING`.
3. **Bootstrapping Integrity (Invariant R3):** Bootstrapping nodes cannot serve downstream staged snapshots (`RES` rejected with `ERR RES FORBIDDEN`) or accept normal local clients.
4. **Orthogonality of Readiness and Health (Invariant R4):** `READY + OK`, `READY + DEGRADED`, and `READY + STALE` are valid independent states. Losing upstream does not erase durable `udb_ready=1`.
5. **Direct Authority & Hop-by-Hop S2S (Invariant R5):** UDB staged synchronization is strictly hop-by-hop. In topology `Services A -> Hub B -> Leaf C`, B selects A as its direct authority, and C selects B as its direct authority. Staged `BEGIN`/`PUT`/`END`/`RES` synchronization is never a transparent multi-hop routed transaction. A direct peer authorizes exports only when `HEL 4 <prop>` specifies `<prop> == me.name` (or `?` during bootstrap).
6. **Round Isolation & Lifecycle (Invariant R6):** Every reconciliation round has an explicit `round_id` and isolated bitmasks (`compared_blocks`, `divergent_blocks`, `completed_blocks`). Masks reset when starting a new round even with the same peer. Staged `END` commits verify `session->round_id == udb_reconcile.round_id`.
7. **Reconciliation Convergence Check (Invariant R7):** Transition to `READY` requires all 6 blocks compared in the active round, all divergent blocks committed in that round, and no active or pending sync sessions.
8. **Bounded Pending RES State (Invariant R8):** Requested sync state (`pending_from`, `pending_deadline`, `pending_round_id`) is bounded and tracked separately from active `session`. Only a same-round `ERR`, plus `BEGIN`, timeout, peer disconnect, policy changes, and shutdown, can clean up pending state. Current-round failures schedule a bounded exponential-backoff retry.
9. **Legacy Storage Migration:** When `.udb_state` is absent, UDB accepts only a complete, loadable six-snapshot legacy database (with or without `.udb_ready`), materializes one generation, atomically writes `.udb_state` with `STATE=READY`, derives `LAST_SYNC`, and logs `UDB_LEGACY_MIGRATION`. Partial or corrupt legacy storage remains NOT_READY.
10. **Crash Consistency:** Atomically renames `.udb_state.tmp` to `.udb_state` and invokes `fsync` on the containing directory descriptor before closing.

### 2.5 Operational Health State Machine (OK / DEGRADED / STALE)
UDB features a deterministic health state machine to manage node reliability:
*   **`OK`**: An eligible upstream propagator candidate is directly connected and `HEL 4`-confirmed (or the local node itself is the designated authoritative propagator, or no policy is configured). Full database synchronization and client operations proceed normally.
*   **`DEGRADED`**: A propagator policy is present (via local config or block `S`), but no eligible candidate is currently usable. The node operates within the configurable grace period (`stale-timeout`, default 300s). Live hop-by-hop mutations (`INS`/`DEL`/`DRP`/`OPT`) continue to propagate normally, existing clients remain connected, and new local clients are accepted.
*   **`STALE`**: The grace period has expired without an eligible propagator. If `stale-action deny-new-clients` is active (default), new incoming local client connections are cleanly rejected with a fatal error notice before registration (`HOOKTYPE_PRE_LOCAL_CONNECT`). Existing clients and S2S links are strictly never disconnected.

**Key Invariants:**
1.  **Strict Trust Invariant:** Elapsed time *never* converts advertised `HEL 4 -` into `HEL 4 ?` or relaxes trust rules. An isolated node with an obsolete policy will never automatically accept staged snapshots from unauthorized neighbors.
2.  **Automatic Recovery:** As soon as an eligible propagator links and confirms `HEL 4`, the node immediately transitions back to `OK`, emits log notice `UDB_SYNC_RECOVERED`, and permits new client connections without requiring an IRCd restart.
3.  **Administrative Override:** Administrators can recover a stale node by updating `propagator "<new-server>";` in `unrealircd.conf` and issuing `/REHASH`. The local config override takes precedence over obsolete database policies.

### 2.6 Diagnostic & Oper Status Commands
Operators can query live synchronization health in real time via `/UDB STATUS` or `/DBQ STATUS` (oper only):
```text
/UDB STATUS
```
Output:
```text
:server 339 oper :UDB synchronization: OK | DEGRADED | STALE
:server 339 oper :Database readiness: READY | BOOTSTRAPPING
:server 339 oper :Serving downstream: YES | NO
:server 339 oper :Selected direct source: <server> | none
:server 339 oper :Configured authority: <authority> | none
:server 339 oper :Advertised state: HEL 4 <server|?|->
:server 339 oper :Policy source: local | S | none
:server 339 oper :Policy: <list>
:server 339 oper :Time without propagator: <seconds>
:server 339 oper :New local clients: ALLOWED | DENIED
:server 339 oper :Last successful synchronization: <timestamp> | none
```

### 2.7 DBQ Secret Redaction

`DBQ` requires oper privileges and never returns the value of `pass`,
`challenge`, or `encryption_key`. Direct queries and child listings show
`<redacted>` for those records.

---

## 3. Credits and License

**Author & Lead Developer:**
The UDB module, its modern modular architecture for UnrealIRCd 6, and the v4 protocol extensions are developed and maintained by **David Abuín Fontán ('davidlig')** (<https://github.com/davidlig/unrealircd-udb>).

**Original Concept & Idea:**
Based on the original UDB protocol concept conceived by **Trocotronic** (*www.redyc.com*).
