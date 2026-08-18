# UDB (Unreal DataBase) for UnrealIRCd 6

A high-performance, distributed Unreal DataBase (UDB) protocol module for UnrealIRCd 6. It provides robust, real-time synchronized data storage across the IRC network for nicks, channels, IPs, and global settings without requiring external services.

Originally authored by **Trocotronic**, this modern rewrite for the UnrealIRCd 6.x modular architecture was developed by **David Abuín Fontán ('davidlig')**.

---

## Features

- **Decentralized Services:** Operates without external databases (like MySQL) or heavy external IRC Services. Data is stored directly at the protocol level.
- **High Performance:** Utilizes an O(1) bitwise-masked hash table, memory-efficient string pooling (key interning), and highly flattened execution paths.
- **Hot-Sync Engine:** Administrative changes (founder, vhost, modes) applied to users or channels via the UDB sync protocol take effect instantly across the network without requiring reconnects.
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

---

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
