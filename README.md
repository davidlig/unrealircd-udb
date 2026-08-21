# UDB (Unreal DataBase) for UnrealIRCd 6

A high-performance, distributed Unreal DataBase (UDB) protocol module for UnrealIRCd 6. It provides robust, real-time synchronized data storage across the IRC network for nicks, channels, IPs, and global settings without requiring external services.

Originally authored by **Trocotronic**, this modern rewrite for the UnrealIRCd 6.x modular architecture was developed by **David Abuín Fontán ('davidlig')**.

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

udb {
    propagator "ares-services.yournetwork.net";
};
```

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
- `C::<#channel>::persistent` sets native `+P` when `chanmodes/permanent` is
  loaded. UDB does not emulate persistence if that handler is unavailable.
- `C::<#channel>::options *1` protects locally-added `+b` entries from removal
  by anyone other than their recorded owner, an identified founder, or an oper.
- `C::<#channel>::options *2` rejects every local `MODE` and `SAMODE` change
  from anyone other than the identified founder. UDB mode locks are command
  overrides, not a textual MODE parser.

### IP Policies

- `I::<ip-or-host>::nolines <types>` creates an UnrealIRCd ban exception using
  the child value, for example `GZQSTmc`. UDB only removes exceptions it created.
- Include `c` in `nolines` to exempt that IP/host from UDB's clone throttle.
- `I::<ip-or-host>::host <hostname>` overrides local clients and restores their
  original host fields when the record is replaced, removed, or the module unloads.

### Settings and Links

- `S::quit_clones <message>` supplies the UDB clone-limit disconnect message.
  `S::quit_ips <message>` is retained as validated settings state for IP-limit
  handling; no separate IP-limit hook currently consumes it.
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
  `nick!user@host` form. They drive the corresponding service identity helpers.
- The only supported `S` values are `quit_ips`, `quit_clones`, `flood`,
  `encryption_key`, `suffix`, `nickserv`, `chanserv`, and `ipserv`.
- The only supported `L` child is `L::<server>::options`: `*1` enables UDB
  debug notices and `*2` selects that server as propagator. `prefix` and
  `allow_clients` are not supported UDB settings.
- Select exactly one UDB propagator source: either `udb::propagator` or one
  `L::<server>::options` record with the propagator bit. Zero or multiple sources
  reject remote UDB writes. Debug notices redact diagnostic detail.


---

## Tests

Run these from the UnrealIRCd source root after building `udb.so`:

```bash
# One node: runtime nick/channel reconciliation against a prepared server.
python3 src/modules/third/udb/tests/runtime_channel_nick.py

# Two nodes: HEL 4, staged synchronization, and authorized real-time INS/DEL.
python3 src/modules/third/udb/tests/two_node_udb.py

# Two nodes: prove a failed live INS snapshot leaves B unchanged.
python3 src/modules/third/udb/tests/two_node_udb.py --runtime-rename-failure

# Two nodes: prove a failed live OPT snapshot leaves B unchanged and returns ERR.
python3 src/modules/third/udb/tests/two_node_udb.py --runtime-opt-rename-failure

# Three nodes: prove A -> B commits before B -> C propagation.
python3 src/modules/third/udb/tests/three_node_udb.py
```

The one-node smoke test is a client fixture: preload its isolated server with
only Argon2id, SHA-256, or crypt credentials and set `UDB_TEST_HOST` /
`UDB_TEST_PORT` if needed. The two-node harness builds and loads the test-only
mutator on authoritative node A; it emits authorized `INS` then `DEL` to node
B after HEL and the staged snapshot settle. The three-node harness has no
mutator: A is the sole seeded source, B must commit A's marker, and only then
does C start so the B-to-C staged path is observable. See the matching files in
`tests/` for isolation prerequisites, `--timeout`, and `--keep`.

## Testing Server

You can see UDB in action and test its real-time capabilities on our official testing network:
- **Server:** `irc.davidlig.net`
- **Ports:** `6697` (SSL/TLS)
- **Services:** Powered by [Ares-IRC-Services](https://github.com/davidlig/ares-irc-services)

---

## Credits

- **Original UDB Protocol Author:** Trocotronic (*www.redyc.com*)
- **UnrealIRCd 6.x Port & Refactor:** David Abuín Fontán ('davidlig')
- **Project URL:** https://github.com/davidlig/unrealircd-udb
