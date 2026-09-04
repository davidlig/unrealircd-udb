# UDB 4 (Unreal DataBase) - Technical Specification & Protocol

**English** | [Español](udb_technical_es.md) | [Project README](../README.md)

This document describes the database model, runtime behavior, persistence rules,
and Server-to-Server (S2S) protocol of **UDB 4.0.0** for UnrealIRCd 6.2.x. It is
intended for UDB maintainers and developers implementing compatible IRC
Services or server integrations.

## 1. Database Architecture

UDB uses an in-memory tree structure and flat-text file storage for persistence. The database is divided into "Blocks", identified by a letter (N, C, I, K, S, L).

### 1.1 Storage Format (Plain Text) & Path Encoding
Disk storage (`udb_X.db`) follows a flat hierarchical structure:
```text
; UDB Block N - Version 1
; Generation: 1
; Saved: 1786942751
; Records: 5
davidlig::pass sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
davidlig::vhost admin.davidlig.net
davidlig::oper netadmin
```
*   Sub-levels are separated by the `::` delimiter.
*   To support IPv6 addresses (e.g. `2001:db8::1`) and special characters without ambiguity, individual path components are canonically percent-encoded (`%XX`, e.g. `2001%3Adb8%3A%3A1`) in disk files, S2S frames, and CRC32 checksum input. In memory, UDB transparently decodes path components to enable native IP matching and lookups.
*   An asterisk `*` prefix in the value indicates an unsigned decimal value representable by C `unsigned long`, with strict format and range validation.
*   The absence of `*` indicates that the data is a text string.

Readiness is recorded separately in `.udb_state`. The current format contains
exactly `FORMAT`, `STATE`, `ORIGIN`, `GENERATION`, and `LAST_SYNC`. A `READY`
marker is accepted only when all six snapshots are valid and carry the same
non-zero generation.

### 1.2 Database Directory, Configuration & Transactional Loader

#### Directives in the `udb { }` block

| Directive | Allowed value | Default | Purpose |
|---|---:|---:|---|
| `database-directory` | Local path | `PERMDATADIR` | Directory containing `udb_N.db`, `udb_C.db`, `udb_I.db`, `udb_S.db`, `udb_L.db`, `udb_K.db`, and `.udb_state`. Relative paths resolve beneath `PERMDATADIR`; a missing directory is created with mode `0700`. |
| `propagator` | One valid server name | None | Strict local authority override. A remote value is usable only while it is a directly connected, HEL-confirmed server. |
| `max-global-clones` | `0` to `1000000` | `0` | Configuration-level global clone limit. |
| `password-flood` | Positive `attempts:seconds` | `5:60` | Per-profile and source-IP credential failure limit. |
| `max-staged-records` | `1` to `10000000` | `500000` | Maximum records accepted in one staged block transaction. |
| `max-staged-bytes` | `1024` to `1073741824` | `67108864` | Maximum serialized bytes accepted in one staged block transaction. |
| `sync-inactivity-timeout` | `1` to `86400` seconds | `60` | Maximum inactivity between staged-sync frames. |
| `sync-absolute-timeout` | `1` to `86400` seconds | `300` | Absolute lifetime of a staged-sync transaction. |
| `stale-timeout` | `1` to `604800` seconds | `300` | Time a non-READY bootstrap may remain `DEGRADED` before becoming `STALE`. |

`database-directory` rejects URLs, CR/LF, empty paths, and paths that cannot fit
the UDB filenames. `propagator` rejects whitespace, CR/LF, overlong names, and
names rejected by UnrealIRCd's server-name validator.

**Transactional loader:** During startup, UDB parses every snapshot into an
isolated candidate tree. No candidate is published and no runtime effect is
applied until the complete six-block set and `.udb_state` are accepted together.
A new standalone/local-authority node with no snapshots can initialize and
persist an empty generation. A follower with no snapshots remains
`BOOTSTRAPPING` until an authorized authority supplies them. Missing members of
an existing set, generation mismatches, malformed state, parse failures,
permission errors, and I/O failures leave the module loaded but non-READY; the
candidate set is discarded and existing files are not overwritten. Failure to
initialize the database engine itself, such as being unable to create its data
directory, causes module loading to fail.

### 1.3 Supported Blocks and Their Options

#### Block N (Nicks - Users)
Stores configurations for registered users.
*   **pass**: User password hash. The accepted stored forms are
    `argon2id:$argon2id$...`, `sha256:<64-hex-digest>`, and `crypt:<hash>`.
    Plaintext, unprefixed hashes, MD5, bcrypt, and unknown formats fail closed.
*   **challenge**: Optional credential method: `argon2id`, `sha256`, or `crypt`.
    When present, it must match the stored password format.
*   **access**: Optional comma- or whitespace-separated IPv4/IPv6 CIDR list.
    A successful NICK or GHOST credential check must also match this list.
*   **vhost**: Custom virtual host applied on connect.
*   **forbid**: Reason that prevents use of the registered nickname.
*   **suspended**: Reason that marks the identified account as suspended.
*   **oper**: IRCop class name (`operclass`, e.g., `locop`, `globop`, `admin`, `services-admin`, `netadmin`, or `-with-override` variants). Validated against local configuration via `find_operclass()`.
*   **swhois**: Extra line in the user's /WHOIS output.
*   **snomasks**: Snomasks to apply automatically.
*   **modes**: User modes to enforce upon authentication.

#### Block C (Channels)
*   **founder**: Nickname of the original channel founder (granted +q automatically).
*   **modes**: Channel modes managed by UDB. Parameters follow the mode string. Mode letters and parameter counts are strictly validated (e.g. `+ntMl` without parameters is rejected, requiring `+ntMl 50`). The native `MAXMODEPARAMS` limit is 12: requests with 13 parameters are rejected before persistence or effects, rather than partially applied. Deleting via `DEL` intentionally does not revert or reconcile live channel modes.
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

##### Live Channel Reconciliation

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

Spamfilter patterns may be stored as plain regex strings. To store a pattern
using base64, prefix standard,
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
    `S::propagator services.example.net,hub1.example.net`). Spaces are
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
2. **Persisted priority list:** `S::propagator pri,sec` selects the first
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
Real-time mutations must likewise originate from the selected direct authority
and are rejected for a block while its staged transaction is active.

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
`HEL 4 <selected-propagator> <epoch16> OCL` request. Only the matching direct
`HEL 4 ACK <selected-propagator> <epoch16> OCL` confirms UDB V4 for that link; no `INF`, staged frame, or
forwarded UDB DB frame is sent first. A missing acknowledgement times out after
60 seconds and automatically aborts the link with `SQUIT`. The `OCL` token is
mandatory. A request or acknowledgement without the required epoch and OCL
token cannot confirm the capability, and the link is rejected immediately or
when the HEL deadline expires. `HEL` is the only DB frame accepted before
confirmation and is never routed beyond the direct link.

**HEL (Capability Negotiation and Auto-Bootstrap):**
`:<sid> DB <direct-peer-sid> HEL 4 <selected-propagator> <epoch16> OCL [OCLG]`

The selected propagator field is `?` only when neither propagator source is
configured and the node is not yet READY; it allows that node to discover
cluster authority and authorizes
the single exclusive bootstrap peer to supply the initial staged snapshot. A node
that is READY without any policy is a standalone authority and advertises its
own name instead of `?`. A configured but
unavailable policy is advertised as `HEL 4 - <epoch16> OCL`, not converted to `?`; `-` grants
no staged-sync authorization and therefore cannot silently broaden access.

The optional `OCLG` token declares the peer as a consumer of the global
operclass view (see section 2.2). Being a ULine/Services peer does not imply
subscription: only a HEL carrying an explicit `OCLG` receives the projection.
The OCLG capability does not make the consumer an OCL consensus participant.

**HEL acknowledgement:**
`:<sid> DB <direct-peer-sid> HEL 4 ACK <selected-propagator> <epoch16> OCL [OCLG]`

`epoch16` identifies the loaded UDB module instance. A repeated advertisement
with the same epoch is idempotent. A changed epoch is a reload boundary: the
peer withdraws only that direct origin's OCL inventory, resets instance-scoped
replay/subscription latches, and exchanges HEL again over the surviving SERVER
connection. The ACK carries the complete advertisement so both directions
recover without polling or reconnecting.

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

After HEL 4 confirmation, `RES` is answered with a staged transaction:

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

### 2.2 Distributed Operclass Registry (OCL / OCLG)

OCL is the distributed source of truth about the operclasses each participant
IRCd currently has loaded from `operclass {}`. OCLG is a derived projection
(the intersection of every inventory sharing the same effective digest)
published to subscribed consumers, typically Services. Neither is ever
persisted to the `udb_*.db` blocks.

**Participants:** the local server and every visible IRCd server that is not a
ULine. OCL membership is explicitly recorded on `SERVER_CONNECT`, reconciled on
module load, and removed before recomputing the view on `SERVER_QUIT`; descendants
removed during a netsplit are purged as well. Each originSID is the sole
authority of its own inventory.

**Effective fingerprint:** each class is canonicalized from the runtime
structure (name, parent, ACL tree with ALLOW/DENY and variables, evaluation
order) and hashed with SHA-256, recursively including the parent's effective
digest. Classes with a missing parent, a cycle, excessive depth, or an
unserializable structure are omitted together with their descendants; the rest
of the inventory remains valid.

**OCL inventory (origin → all HEL-confirmed non-ULine peers):**

```text
:<sourceSID> DB * OCL BEGIN <originSID> <epoch16> <generation> <count> <inventory_digest>
:<sourceSID> DB * OCL ITEM <originSID> <epoch16> <generation> <operclass> <effective_digest>
:<sourceSID> DB * OCL END <originSID> <epoch16> <generation>
```

Reception semantics:

- Reception is atomic: `BEGIN` creates an isolated stage, `ITEM` frames fill it,
  and only a valid `END` (exact count, valid unique names, 64-character
  hexadecimal digests, and a matching `inventory_digest`) commits atomically.
  The snapshot is forwarded to other peers only after the commit.
- `epoch16` identifies the instance that emitted the inventory (fresh on every
  module load); `generation` is monotonic within the epoch. An independent
  high-water mark retains the epoch, generation, count, and digest of the newest
  observed generation. A lower generation is stale even if its newer stage was
  aborted or expired; descriptors for lower generations are not retained.
- A new epoch from the same originSID immediately invalidates the previous
  inventory, and any later frame from a superseded epoch is treated as stale. A
  `/REHASH` unloads and reloads UDB, so it starts a new OCL instance and rebuilds
  the registry through HEL and replay.
- Accepting a newer `BEGIN` makes the previous snapshot stop participating in
  the GLOBAL computation immediately; if the new stage aborts or expires
  (`UDB_OCL_STAGE_TIMEOUT`, 30s), the origin stays without a current inventory
  until a later valid snapshot arrives.
- The same high-water epoch/generation with a different count or digest is a
  protocol violation, including after a stage abort; the frame is ignored
  without replacing state. An identical descriptor may be retransmitted after
  an aborted attempt.
- Every frame is accepted only when originSID is a visible participant server
  and the frame arrived over the link that reaches it (`origin->direction`).
- When a server disappears (SERVER_QUIT/SQUIT), its membership, inventory,
  stage, watermark, and epochs are removed immediately; the global view is
  recomputed only over current members.
- Each effective local inventory change replaces local state, recomputes OCLG
  immediately, and then broadcasts the new OCL. A rehash with no effective
  change does not create another generation within that instance.
- After HEL completes, a non-ULine peer receives a replay of the local inventory plus
  every committed remote inventory; it never needs to poll node by node.

**Global view OCLG (only to peers that declared `OCLG` in HEL):**

```text
:<sid> DB <consumerSID> OCLG BEGIN <epoch16> <generation> <READY|INCOMPLETE> <count> <view_digest>
:<sid> DB <consumerSID> OCLG ITEM <epoch16> <generation> <operclass> <effective_digest>
:<sid> DB <consumerSID> OCLG END <epoch16> <generation>
```

An operclass is GLOBAL only when the registry is complete (every current OCL
member holds a current inventory) and the effective digest matches everywhere.
Every effective change is delivered as a full atomic snapshot; an
INCOMPLETE snapshot carries zero entries so the consumer can swap atomically and
withdraw all previous availability without partial windows. The OCLG generation
is local to the emitting node and only increases on effective changes; a new
subscriber receives the current snapshot immediately after HEL.

**Operator observability:**

```text
/UDB OPERCLASSES [filter]   Registry state and per-server inventories
/UDB OPERCLASS <name>       GLOBAL availability and per-participant digest
```

Relevant log events: `UDB_OCL_LOCAL_CHANGED`, `UDB_OCL_REMOTE_COMMITTED`,
`UDB_OCL_STAGE_ABORT`, `UDB_OCL_REGISTRY_INCOMPLETE`, `UDB_OCL_REGISTRY_READY`,
`UDB_OCL_GLOBAL_ADD`, `UDB_OCL_GLOBAL_DEL`, and `UDB_OCL_PROTOCOL_VIOLATION`.
This registry is runtime state and does not affect `udb_ready` nor the
convergence of the UDB blocks; an operclass divergence never prevents the
distributed database from converging.

### 2.3 Real-time Data Modification
To inject or delete records on the fly, the following commands are used (usually with target `*` for broadcast).

**INS (Insert / Modify):**
`:<sid> DB * INS <block_letter>::<key>[::<subkey>] <value>`
*Example:* `:<sid> DB * INS N::davidlig::vhost admin.davidlig.net`

**DEL (Delete):**
Deletes a node and cascades to children.
`:<sid> DB * DEL <block_letter>::<key>[::<subkey>]`
*Example:* `:<sid> DB * DEL C::#opers::topic`

After HEL 4 confirmation, real-time `INS`, `DEL`, `DRP`, and `OPT` are accepted
only from the selected direct propagator. They are rejected while that block has
a staged transaction and are persisted and forwarded only to HEL-confirmed
direct peers.

UDB strictly validates all records received via `INS`, `PUT`, or loaded from disk
against a declarative per-block schema catalogue and strict numeric limits. Unknown keys,
invalid hierarchy nesting (such as composite paths in Block S), or incompatible data types are
immediately rejected with `ERR INS 2 <correlation_id> <block>` or `ERR PUT 2 <round_id> <block>` (`UDB_ERR_PARAMS`),
and cause local `.db` file parsing to abort fail-closed (discarding candidate changes and leaving
the database uncorrupted).

Mutation and audit diagnostics retain the path and safe failure context but redact values for
`S::encryption_key`, `N::<nick>::pass`, and `C::<channel>::pass` / `challenge`. Clone limits
must be representable as native `int` values (`0` through `INT_MAX`), and server-ban user and
host components must each fit UDB's 127-byte native boundary. Values outside these limits are
rejected before a snapshot, runtime effect, or partial truncation can occur. The same record
validation applies during startup, so a persisted over-capacity `C::<channel>::modes` policy
rejects the complete candidate set, keeps source snapshots byte-preserved, and leaves the node
non-READY until authoritative recovery supplies a valid six-block generation.

### 2.4 Numeric Limits & Mathematical Hierarchy

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
- Prefixed raw component (`b64:`): $4 + 4096 = 4100$ bytes ($\le 4608$; `UDB_COMPONENT_RAW_MAX`).
- Percent-encoded component (`b64%3A`): $4100 + 2 = 4102$ bytes ($\le 4608$; `UDB_COMPONENT_ENCODED_MAX`).
- Full path (`K::F::b64%3A...::reason`): $4116$ bytes ($\le 8192$; `UDB_RECORD_PATH_MAX`).
- Serialized disk record line: $4116 + 1 + 4096 + 1 = 8214$ bytes ($\le 12320$; `UDB_RECORD_LINE_MAX`).
- S2S wire frame: $8214 + 256 = 8470$ bytes ($\le 16384$; `UDB_S2S_LINE_MAX`).

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

### 2.5 Error Handling (ERR)
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

### 2.6 Operational Readiness, Persistence & Health (Invariants R1 - R10)

UDB enforces deterministic invariants for database readiness, durable persistence, round-isolated reconciliation, and hop-by-hop topology:

1. **Durable Database Readiness (Invariant R1):** `udb_ready=1` is reachable only after all 6 blocks (`N, C, I, S, L, K`) are durably persisted to disk, `.udb_state` is atomically persisted to `STATE=READY` (with directory `fsync`), and then `udb_ready=1` is set.
2. **Missing Snapshot Check on Restart (Invariant R2):** On restart with `.udb_state` indicating `READY`, if any required block snapshot `udb_X.db` is missing (`ENOENT` / `UDB_LOAD_EMPTY`) or has a different generation, the node does not start `READY`; it logs `UDB_READY_INCOMPLETE` and fails closed to `BOOTSTRAPPING`.
3. **Bootstrapping Integrity (Invariant R3):** Bootstrapping nodes cannot serve downstream staged snapshots (`RES` rejected with `ERR RES FORBIDDEN`) or accept normal local clients.
4. **Orthogonality of Readiness and Health (Invariant R4):** `READY + OK` and `READY + DEGRADED` are valid states. `STALE` is exclusive to a non-READY bootstrap. Losing upstream does not erase durable `udb_ready=1`.
5. **Direct Authority & Hop-by-Hop S2S (Invariant R5):** UDB staged synchronization is strictly hop-by-hop. In topology `Services A -> Hub B -> Leaf C`, B selects A as its direct authority, and C selects B as its direct authority. Staged `BEGIN`/`PUT`/`END`/`RES` synchronization is never a transparent multi-hop routed transaction. A direct peer authorizes exports only when `HEL 4 <prop>` specifies `<prop> == me.name` (or `?` during bootstrap).
6. **Round Isolation & Lifecycle (Invariant R6):** Every reconciliation round has an explicit `round_id` and isolated bitmasks (`compared_blocks`, `divergent_blocks`, `completed_blocks`). Masks reset when starting a new round even with the same peer. Staged `END` commits verify `session->round_id == udb_reconcile.round_id`.
7. **Reconciliation Convergence Check (Invariant R7):** Transition to `READY` requires all 6 blocks compared in the active round, all divergent blocks committed in that round, and no active or pending sync sessions.
8. **Bounded Pending RES State (Invariant R8):** Requested sync state (`pending_from`, `pending_deadline`, `pending_round_id`) is bounded and tracked separately from active `session`. Only a same-round `ERR`, plus `BEGIN`, timeout, peer disconnect, policy changes, and shutdown, can clean up pending state. Current-round failures schedule a bounded exponential-backoff retry.
9. **Orphaned Snapshots Fail Closed:** When `.udb_state` is absent, even a complete, loadable six-snapshot database remains `BOOTSTRAPPING` (clients denied), logs `UDB_ORPHANED_SNAPSHOTS`, and requires an authorized bootstrap or explicit operator action. Only the complete current state format is accepted.
10. **Crash Consistency:** Atomically renames `.udb_state.tmp` to `.udb_state` and invokes `fsync` on the containing directory descriptor before closing.

### 2.7 Operational Health State Machine (OK / DEGRADED / STALE)
UDB features a deterministic health state machine to manage node reliability. Client admission is gated exclusively by database readiness (`udb_ready`): a READY node always accepts new local clients regardless of synchronization health, and a node without READY always denies them.

*   **`OK`**: No known divergence with the authority is pending resolution. This does NOT require the propagator to be online: a READY node whose propagator is offline (e.g. services under maintenance) remains `OK` and fully operational. New local clients are accepted.
*   **`DEGRADED`**:
    *   Without `READY`: bootstrap is still pending. The node ages through the configurable grace period (`stale-timeout`, default 300s) toward `STALE`. New local clients are denied (`udb_ready == 0`).
    *   With `READY`: confirmed divergence with the authority is being recovered (reconciliation active or retries pending). The node keeps serving its last complete database; new local clients are accepted.
*   **`STALE`**: Only reachable without `READY`: the bootstrap grace period has expired. New local clients remain denied (`udb_ready == 0`). `READY + STALE` is an invalid state.

**Key Invariants:**
1.  **Sole Admission Gate:** Only `udb_ready` decides client admission. Synchronization health (`OK`/`DEGRADED`/`STALE`) never restricts clients, and existing clients and S2S links are strictly never disconnected by health transitions.
2.  **Strict Trust Invariant:** Elapsed time *never* converts advertised `HEL 4 -` into `HEL 4 ?` or relaxes trust rules. A node whose configured authority is unavailable never accepts staged snapshots from unauthorized neighbors.
3.  **Automatic Recovery:** As soon as an eligible propagator links and confirms `HEL 4`, a bootstrap-pending node converges to `READY` + `OK`, emits log notice `UDB_SYNC_RECOVERED`, and permits new client connections without requiring an IRCd restart.
4.  **Administrative Override:** Administrators can recover a bootstrap-pending node by updating `propagator "<new-server>";` in `unrealircd.conf` and issuing `/REHASH`. The local configuration override takes precedence over the persisted `S::propagator` policy.

### 2.8 Diagnostic & Oper Status Commands
Operators can query live synchronization health in real time via `/UDB STATUS` or `/DBQ STATUS` (oper only):
```text
/UDB STATUS
```
Output:
```text
:server 339 oper :Database readiness: READY | BOOTSTRAPPING
:server 339 oper :UDB synchronization: OK | DEGRADED | STALE
:server 339 oper :Recovery: ACTIVE | IDLE
:server 339 oper :Selected propagator: <server> | none
:server 339 oper :Selected direct source: <server> | none
:server 339 oper :Advertised state: HEL 4 <server|?|->
:server 339 oper :Serving downstream: YES | NO
:server 339 oper :Policy source: local | S | none
:server 339 oper :Policy: <list>
:server 339 oper :Configured authority: <authority> | none
:server 339 oper :Time without propagator: <seconds>
:server 339 oper :New local clients: ALLOWED | DENIED
:server 339 oper :Last successful synchronization: <timestamp> | none
```

### 2.9 DBQ Secret Redaction

`DBQ` requires oper privileges and never returns the value of `pass`,
`challenge`, or `encryption_key`. Direct queries and child listings show
`<redacted>` for those records.

---

## 3. Verification

The canonical build and test matrix is maintained in
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml). Focused and runtime
harnesses live in [`tests/`](../tests/); the harness-specific Markdown files
document their isolation requirements. Documentation-only changes do not require
rebuilding the module, but relative links, version references, and Markdown
formatting must be checked before release.
