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
*   **pass**: User password (plain text or hash like `sha256:hash`).
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
*   **options**: Numeric channel options, including the mode lock flag.

### 1.3 Live Channel Reconciliation

UDB owns the founder `+q` state of a registered channel. When `founder` is
replaced, UDB removes `+q` from the previous present founder and grants it to
the new identified founder. A founder never receives `+o` from UDB.

`pass` and `challenge` authenticate a joining user for that channel and grant
only `+a` for the current membership. They are not a source of `+o`. Replacing
or deleting either credential revokes `+a` only when it was granted by UDB.
Deleting the channel profile revokes UDB-managed founder and channel-admin
privileges and clears its persistent topic.

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
When a server connects to another, block states are verified using CRC32.

**INF (Block Information):**
`:<sid> DB <target> INF <block_letter> <crc32_hex> <timestamp>`

**RES (Sync Request):**
`:<sid> DB <target> RES <block_letter>`

**FDR (End of Summary):**
Marks the end of a mass block transmission.
`:<sid> DB <target> FDR <block_letter>`

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
