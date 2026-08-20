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
davidlig::oper *4
```
*   Sub-levels are separated by the `::` delimiter.
*   An asterisk `*` prefix in the value indicates that the data is numeric (integer).
*   The absence of `*` indicates that the data is a text string.

### 1.2 Supported Blocks and Their Options

#### Block N (Nicks - Users)
Stores configurations for registered users.
*   **pass**: User password hash. Only Argon2id (`$argon2id$...`) and bcrypt
    (`$2a$`, `$2b$`, or `$2y$`) are accepted; optional `argon2id:` and
    `bcrypt:` prefixes are supported. Plaintext and legacy digest values fail
    closed.
*   **access**: Optional comma- or whitespace-separated IPv4/IPv6 CIDR list.
    A successful NICK or GHOST credential check must also match this list.
*   **vhost**: Custom virtual host applied on connect.
*   **oper**: IRCop level (`*1` = Helper, `*2` = Admin, `*4` = Root).
*   **swhois**: Extra line in the user's /WHOIS output.
*   **snomasks**: Snomasks to apply automatically.
*   **modes**: User modes to enforce upon authentication.

#### Block C (Channels)
*   **founder**: Nickname of the original channel founder (granted +q automatically).
*   **modes**: Channel modes managed by UDB. Parameters follow the mode string.
*   **topic**: The persistent channel topic.
*   **access**: Child records keyed by the identified nicknames allowed to join.
*   **forbid**: Channel prohibition reason.
*   **suspended**: Disables registered-channel founder and `+r` behavior.
*   **pass** and **challenge**: Channel-admin authentication credential.
*   **persistent**: Sets native `+P` when the permanent-channel mode handler is loaded.
*   **options**: Numeric channel options: `*1` protects locally-added bans,
    and `*2` locks mode changes to the identified founder.

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

All UDB credential checks accept only `challenge` values `argon2id` (`argon2`
is an alias) and `bcrypt`, and use UnrealIRCd's native authentication API. A
missing challenge is accepted only when the stored hash has an unambiguous
Argon2id or bcrypt prefix. Plaintext, MD5, SHA-256, Unix crypt, and unsupported
challenge names fail closed. Failed checks are bounded and rate-limited by the
active `S::flood` or `udb::password-flood` setting per profile and source IP.

UDB uses command overrides for `MODE` and `SAMODE`, rather than raw command
text inspection. With channel option `*1`, locally-added `+b` masks are tracked
with their setter and cannot be removed by another local user, except an
identified founder or an oper.

#### Block I (IPs and Hosts)
*   **clones**: Numeric limit of simultaneous connections (`*<number>`).
*   **host**: Host override applied before a local connection completes.
*   **nolines**: Sanction exemption letters (e.g., `GZT` to exempt from G-Lines, Z-Lines, etc.).

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
*   **clones**: Global numeric clone limit for IPs not specified in Block I.
*   **challenge**: Default hash type for passwords.
*   **quit_clones**: Quit message for connections dropped due to clone limits.
*   **bot_nick**: Virtual NickName for UDB system messages (e.g., `UDB-Bot`).
*   **bot_mask**: Virtual mask for the system bot (e.g., `services@network.com`).

#### Block L (S2S Links)
*   **options**: Numeric options via bitmask (`*1` enables S2S debug logs).

---

## 2. S2S (Server-to-Server) Protocol

The UDB protocol integrates into UnrealIRCd's native S2S traffic using the extended `DB` command.
Only peers advertising the UDB module capability are synchronized. Real-time
mutations must originate from the configured `udb::propagator`; a peer that is
actively serving a block synchronization may only send records for that block.
**General structure:**
`:<source_sid> DB <target> <subcommand> <parameters>`

### 2.1 Initial Synchronization (Handshake)
When a server connects to another, block states are verified using a CRC32 over
the canonical logical records. The digest sorts serialized `path value` records,
so save timestamps, comment headers, and sibling insertion order do not affect
it. UDB module ModData value `2` negotiates the staged V4 transfer capability.

**INF (Block Information):**
`:<sid> DB <target> INF <block_letter> <crc32_hex> <timestamp>`

**RES (Sync Request):**
`:<sid> DB <target> RES <block_letter>`

For peers with the staged capability, `RES` is answered with a transaction:

**BEGIN:** `:<sid> DB <target> BEGIN <block> <txid> <digest>`

**PUT:** `:<sid> DB <target> PUT <block> <txid> <path> :<value>`

**END:** `:<sid> DB <target> END <block> <txid> <digest>`

**ACK:** `:<sid> DB <target> ACK <block> <txid> <digest>`

`PUT` paths omit the block prefix because the block is an explicit parameter.
The receiver builds an isolated tree per block and never applies its runtime
effects during transfer. On `END`, it verifies the canonical digest, writes the
staged tree atomically via the block temporary file, then replaces the active
tree and applies the new runtime effects. A peer quit, 60-second inactivity
timeout, malformed `PUT`, unexpected transaction ID, or bad digest discards the
staged tree only. The prior active and durable tree remain in use.

While a block has a staged transaction, real-time `INS`, `DEL`, `DRP`, and `OPT`
are rejected with `UDB_ERR_SYNC_ACTIVE`, including requests from the propagator.
Outside a transaction those mutations still require the configured propagator.

`FDR` remains only for pre-V4 peers (ModData value `1`). It is not part of the
staged protocol and retains the legacy in-place transfer behavior; mixed V4 and
legacy networks therefore do not get V4 isolation on the legacy link.

### 2.2 Real-time Data Modification
To inject or delete records on the fly, the following commands are used (usually with target `*` for broadcast).

**INS (Insert / Modify):**
`:<sid> DB * INS <block_letter>::<key>[::<subkey>] <value>`
*Example:* `:<sid> DB * INS N::davidlig::vhost admin.davidlig.net`

**DEL (Delete):**
Deletes a node and cascades to children.
`:<sid> DB * DEL <block_letter>::<key>[::<subkey>]`
*Example:* `:<sid> DB * DEL C::#opers::topic`

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

---

## 3. Credits and License

**Original Author (UnrealIRCd 3.x):**
The UDB protocol and its classic original version were conceived and developed by **Trocotronic** (*www.redyc.com*). The current module and protocol optimizations are developed and maintained under the project URL **https://github.com/davidlig/unrealircd-udb** by **David Abuín Fontán ('davidlig')**.

**Current Version (UnrealIRCd 6.2.x - UDB v4.0.0):**
Modern refactoring, transition to the new modular C architecture, and full adaptation to the new UnrealIRCd v6 API developed by **David Abuín Fontán "davidlig"** (2026).
UDB has been successfully converted into a standard and optimized 3rd-party module (`udb.so`), with native integration to the TKL engine, modern cryptographic security, and full support for v6 S2S routing.
