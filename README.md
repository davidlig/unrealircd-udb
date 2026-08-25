# UDB 4 (Unreal DataBase) for UnrealIRCd 6

A high-performance, distributed Unreal DataBase (UDB) protocol module for UnrealIRCd 6. It provides robust, real-time synchronized data storage across the IRC network for nicks, channels, IPs, and global settings without requiring external services.

Developed by **David Abuín Fontán ('davidlig')**, based on the original UDB concept by **Trocotronic**.

---

## Features

- **Decentralized Services:** Operates without external databases (like MySQL) or heavy external IRC Services. Data is stored directly at the protocol level.
- **High Performance:** Utilizes an O(1) bitwise-masked hash table, memory-efficient string pooling (key interning), and highly flattened execution paths.
- **Hot-Sync Engine:** Administrative changes are validated against the configured propagator and reconciled against live users and channels without requiring reconnects.
- **Forced Nick Migrations:** Secure integration that ensures if a registered nickname is taken by an unauthorized user, they are forcibly renamed using `SVSNICK` when the database authenticates the true owner.
- **S2S Sync:** Custom Server-to-Server protocol that automatically negotiates checksums and synchronizes missing blocks when connecting to other UDB-enabled nodes.

---

## Step-by-Step Installation Guide

### Step 1: Download & Install UnrealIRCd 6

If you don't have UnrealIRCd installed yet, download the latest version from the official website:
👉 **[https://www.unrealircd.org/download](https://www.unrealircd.org/download)**

```bash
# 1. Download and extract UnrealIRCd
wget https://www.unrealircd.org/downloads/unrealircd-latest.tar.gz
tar -zxvf unrealircd-latest.tar.gz
cd unrealircd-6.*

# 2. Configure, compile and install UnrealIRCd
./Config
make
make install

# 3. Enter the installed server directory
cd ~/unrealircd
```

---

### Step 2: Install the UDB Module

You can install UDB using either the built-in Module Manager or from source:

#### Option A: Using UnrealIRCd Module Manager (Recommended)

1. Add the UDB module repository to `conf/modules.sources.list`:
   ```text
   https://raw.githubusercontent.com/davidlig/unrealircd-udb/main/modules.list
   ```

2. Download and install UDB automatically with:
   ```bash
   ./unrealircd module install third/udb
   ```

---

#### Option B: Building from Source (Development / Git)

1. Navigate to your UnrealIRCd source directory (where you compiled UnrealIRCd):
   ```bash
   cd /path/to/unrealircd-6.*
   ```

2. Clone this repository directly into `src/modules/third/udb`:
   ```bash
   git clone https://github.com/davidlig/unrealircd-udb.git src/modules/third/udb
   ```

3. Compile, link, and install the module:
   ```bash
   make custommodule MODULEFILE=udb/src/udb
   ln -sf udb/src/udb.so src/modules/third/udb.so
   make install
   ```

---

### Step 3: Configure UnrealIRCd

Add the module loading directive and the `udb` configuration block to your `conf/unrealircd.conf`:

```conf
loadmodule "third/udb";

// Recommended for instant fail-fast rejection during initial server connect:
require module {
    name "third/udb";
};

udb {
    propagator "ares-services.yournetwork.net";
    // Optional: UDB stores udb_N.db, udb_C.db, etc. in this directory.
    database-directory "/var/lib/unrealircd/udb";
};
```

`require module { name "third/udb"; };` ensures that any server attempting to link
into the network without the UDB module loaded is immediately rejected at the
connection handshake, protecting the network from database desynchronization.
Additionally, UDB's native S2S protocol strictly enforces that directly linked
peers complete the `DB HEL 4` capability negotiation within the synchronization
timeout, automatically aborting the link (`SQUIT`) if UDB capability is missing.

`database-directory` accepts a local absolute path or a path relative to
UnrealIRCd's permanent data directory. UDB creates the final directory if it
does not exist and stores every block file directly beneath it. If omitted, UDB
keeps the legacy default of storing block files in UnrealIRCd's permanent data
directory.

---

### Step 4: Test & Start Server

Test your configuration and start the IRC server:

```bash
# Test the configuration file
./unrealircd configtest

# Start (or restart) the IRC server
./unrealircd start
# Or if it is already running:
./unrealircd restart
```

---

## Documentation

Detailed technical documentation is available in the `doc/` directory:
- [Technical Specification (English)](doc/udb_technical_en.md)
- [Especificación Técnica (Español)](doc/udb_technical_es.md)

### Channel Authentication

- UDB accepts only Argon2id (`$argon2id$...`, optionally prefixed with
  `argon2id:`), `sha256:` and `crypt:` hashes. Configure a matching challenge
  as `argon2id`, `sha256`, or `crypt`. Plaintext, MD5, bcrypt, and unrecognized
  challenge names are rejected, including existing stored credentials.
- Failed credential checks are rate-limited per UDB profile and source IP using
  `S::flood` or `udb::password-flood`; the in-memory tracker is bounded.
- Nick-related UDB notices, including invalid credentials and password-flood
  locks, use the connected ULine client matched by `S::nickserv` as their
  prefix. Numeric replies remain server-generated.
- `N::<nick>::access` may contain one or more comma- or whitespace-separated
  IPv4 or IPv6 CIDRs. A valid nick password also requires a matching CIDR for
  `/NICK` and `/GHOST`.
- A channel founder receives only `+q` after identifying with the configured
  UDB nick profile.
- A successful `C::<#channel>::pass` / `challenge` authentication grants only
  `+a` for that channel membership. It does not grant `+o`.
- Replacing or deleting the founder, password, challenge, or complete channel
  profile reconciles the live channel and removes UDB-managed privileges that
  are no longer valid.
- `INVITE <nick> <channel> <password>` validates a channel password and gives a
  local target one five-minute, one-use entry grant. That grant bypasses only
  the UDB password check and never grants `+a`; `JOIN <channel> <password>`
  continues to grant `+a`.
- `C::<#channel>::options *<value>` sets numeric bitmask channel options:
  - `*1` (`0x1` / `UDB_CHOPT_PROTECT_BANS`): Protects locally-added `+b` entries from removal
    by anyone other than their recorded owner, an identified founder, or an oper.
  - `*2` (`0x2` / `UDB_CHOPT_LOCK_MODES`): Absolute mode lock that blocks all local `MODE` and `SAMODE` changes (including for the founder).
  - `*4` (`0x4` / `UDB_CHOPT_LOCK_TOPIC`): Absolute topic lock that blocks all local `TOPIC` changes (including for the founder).
  - `*8` (`0x8` / `UDB_CHOPT_PERSISTENT`): Sets native `+P` when `chanmodes/permanent` is
    loaded (creates the channel on insert if non-existent, and destroys it when disabled or removed if empty).
  - Supports flag combinations such as `*6` (`0x2 | 0x4` = lock modes + lock topic) or `*14` (`0x2 | 0x4 | 0x8`).

### IP Policies

- `I::<ip-or-host>::nolines <types>` creates an UnrealIRCd ban exception using
  the child value, for example `GZQSTmc`. UDB only removes exceptions it created.
- Include `c` in `nolines` to exempt that IP/host from UDB's clone throttle.
- `I::<ip-or-host>::host <hostname>` overrides local clients and restores their
  original host fields when the record is replaced, removed, or the module unloads.
  Live explicit and derived vhost changes are announced by the connected ULine
  client matched by `S::ipserv`.

- `S::clones *<limit>` defines the global default clone limit when no IP-specific limit is configured.
- `S::quit_clones <message>` supplies the UDB clone-limit disconnect message.
- `S::quit_ips <message>` supplies the disconnect message for modular IP-limit subsystems.
- `S::flood <attempts>:<seconds>` overrides the active UDB password-flood
  configuration. Removing it restores the `udb::password-flood` value.
- `S::encryption_key <64-hex-chars>` and `S::suffix <.domain>` enable deterministic
  UDB vhosts for local clients. UDB uses HMAC-SHA-256 over the client's original
  IP and host, emits the first 16 digest bytes as 32 lowercase hexadecimal
  characters, and appends the suffix. Both settings are required; replacing or
  removing either setting immediately updates or restores connected local clients.
  The key is a 256-bit hexadecimal HMAC key stored as a normal UDB setting, not
  an encryption key for UDB files. An explicit `N::<nick>::vhost` takes precedence.
  Suffixes must be valid dotted hostnames and leave room for the 32-character label.
- Service settings (`nickserv`, `chanserv`, and `ipserv`) must be masks in
  `nick!user@host` form. UDB resolves each mask against exactly one connected,
  non-dead ULine user using UnrealIRCd's native user-mask matcher; it never
  fabricates a `Client` or caches the result. If no unique service client is
  available, UDB keeps the event safe by using the local server source and logs
  the fallback.
- ChanServ is the source of UDB channel effects: persistent topics, managed
  channel modes, and member rank changes (`q`, `a`, `o`, `h`, and `v`).
- The only supported `S` values are `clones`, `quit_ips`, `quit_clones`, `flood`,
  `encryption_key`, `suffix`, `nickserv`, `chanserv`, `ipserv`, and `propagator`.
- The only supported `L` child is `L::<server>::options`: `*1` enables UDB
  debug notices.
- Propagator authority is resolved via a fault-tolerant priority model: local
  `udb::propagator` takes precedence, followed by the dynamic priority list in
  `S::propagator` (e.g. `services.net,hub1.net` with automatic failover via `FindServer`),
  and auto-bootstrap for unconfigured clean nodes. Debug notices redact diagnostic detail.
- UDB snapshots are created exclusively with mode `0600`, regardless of umask.
  Platforms that provide `O_NOFOLLOW` also reject a symlink temporary snapshot.
  UDB flushes and `fsync`s the temporary file before rename, then `fsync`s its
  containing directory after rename. Failed creation, writing, file sync,
  closing, or rename removes the temporary file without changing the active
  database. A directory-sync failure is reported after the rename has occurred,
   so the replacement is visible but not confirmed crash-durable.
- Persisted records with an empty `::` path component, an invalid path, or an
  overlong line are logged and skipped; the rest of the block still loads.
- Staged `END` digests must be complete, non-empty hexadecimal values that fit
  in `unsigned long`. Invalid or overflowing input aborts the staged tree and
  leaves the active snapshot unchanged.


---

## Tests

Run these from the UnrealIRCd source root after building `udb.so`:

```bash
# Isolated nick/channel runtime authentication and privilege harness (requires bwrap):
python3 src/modules/third/udb/tests/runtime_channel_nick.py

# Channel options (*6 lock_modes/lock_topic, *8 persistent), modes validation, and notices:
python3 src/modules/third/udb/tests/runtime_mlock_modes.py

# Channel modes INS churn avoidance and founder +q restoration:
python3 src/modules/third/udb/tests/runtime_channel_modes_ins.py

# Global and IP clone limit throttling with custom quit messages:
python3 src/modules/third/udb/tests/runtime_clone_limit.py

# Link debug notices routing (L::<server>::options *1):
python3 src/modules/third/udb/tests/runtime_debug_notices.py

# Propagator dynamic priority list failover and auto-bootstrap:
python3 src/modules/third/udb/tests/test_propagator_failover.py

# Two nodes: deterministic equal-timestamp conflict resolution, staged N/K records, and authorized INS/DEL:
python3 src/modules/third/udb/tests/two_node_udb.py

# Two nodes: staged snapshots revoke a live UDB oper and apply loopback K-line effects:
python3 src/modules/third/udb/tests/staged_runtime_effects.py

# Three nodes: multi-hop A -> B commits before B -> C propagation:
python3 src/modules/third/udb/tests/three_node_udb.py
```

The one-node harness creates its own temporary configuration and UDB data with
valid SHA-256 nick and channel credentials, configtests it, then runs the
installed daemon under `bwrap`. It uses `UDB_TEST_IRCD_ROOT` (default:
`~/unrealircd`) and supports `--ircd`, `--module`, `--timeout`, and `--keep`.
The two-node harness builds and loads the test-only mutator on authoritative
node A; it emits authorized `INS` then `DEL` to node B after HEL and the staged
snapshot settle. The two-node harness seeds divergent
equal-mtime blocks and verifies that the lexicographically higher server SID
wins with one `RES` per divergent block, including a nested K-line replacement. The three-node harness has no
mutator: A is the sole seeded source, B must commit A's marker, and only then
does C start so the B-to-C staged path is observable. See the matching files in
`tests/` for isolation prerequisites, `--timeout`, and `--keep`.

The DEL and DRP rename-failure modes use the test-only `LD_PRELOAD` interposer.
They independently arm the mutator command, require an `ERR` reply, no temporary
snapshot, byte-identical persisted N data, and retained active records.

## Testing Server

You can see UDB in action and test its real-time capabilities on our official testing network:
- **Server:** `irc.davidlig.net`
- **Ports:** `6697` (SSL/TLS)
- **Services:** Powered by [Ares-IRC-Services](https://github.com/davidlig/ares-irc-services)

---

## Credits

- **Author & Lead Developer:** David Abuín Fontán ('davidlig')
- **Original Concept & Idea:** Based on the original UDB concept by Trocotronic (*www.redyc.com*)
- **Project URL:** https://github.com/davidlig/unrealircd-udb
