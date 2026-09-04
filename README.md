# UDB 4 for UnrealIRCd 6

**English** | [Español](README_ES.md)

UDB (Unreal DataBase) is a distributed module for UnrealIRCd 6 that keeps nick,
channel, IP address, global setting, and network sanction records synchronized
in real time. Data is stored directly by the IRC servers and replicated through
a Server-to-Server (S2S) protocol without requiring an external database.

## Installation

UDB requires UnrealIRCd 6.2.x. If UnrealIRCd is not installed yet, follow the
[official installation guide](https://www.unrealircd.org/docs/Installing_from_source).

### Module Manager (recommended)

1. Add the UDB repository to `conf/modules.sources.list`:

   ```text
   https://raw.githubusercontent.com/davidlig/unrealircd-udb/main/modules.list
   ```

2. From the installed UnrealIRCd directory, install the module:

   ```bash
   ./unrealircd module install third/udb
   ```

### Build from source

From the UnrealIRCd source directory:

```bash
git clone https://github.com/davidlig/unrealircd-udb.git src/modules/third/udb
make custommodule MODULEFILE=udb/src/udb
ln -sf udb/src/udb.so src/modules/third/udb.so
make install
```

### Minimal configuration

Add the following to `conf/unrealircd.conf`, replacing the propagator name with
your network's directly connected authority server:

```conf
loadmodule "third/udb";

require module {
    name "third/udb";
};

udb {
    propagator "services.example.net";
};
```

Test the configuration and restart the server:

```bash
./unrealircd configtest
./unrealircd restart
```

Topologies without a local propagator and all advanced options are covered by
the technical documentation.

## Technical documentation

- [Technical specification and protocol (English)](doc/udb_technical_en.md)
- [Especificación técnica y protocolo (Español)](doc/udb_technical_es.md)

## Testing server

UDB can be tested on the official development network:

- TLS server: [irc.davidlig.net:6697](ircs://irc.davidlig.net:6697)
- Services: [Ares IRC Services](https://github.com/davidlig/ares-irc-services)

## Credits

- Author and lead developer: **David Abuín Fontán (`davidlig`)**
- Original UDB concept: **Trocotronic**
- Project: [github.com/davidlig/unrealircd-udb](https://github.com/davidlig/unrealircd-udb)
