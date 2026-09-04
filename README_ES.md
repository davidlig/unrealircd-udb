# UDB 4 para UnrealIRCd 6

[English](README.md) | **Español**

UDB (Unreal DataBase) es un módulo distribuido para UnrealIRCd 6 que mantiene
sincronizados en tiempo real los registros de nicks, canales, direcciones IP,
ajustes globales y sanciones de red. Los datos se almacenan directamente en los
servidores IRC y se replican mediante un protocolo Server-to-Server (S2S), sin
necesidad de una base de datos externa.

## Instalación

UDB requiere UnrealIRCd 6.2.x. Si todavía no está instalado, consulta la
[guía oficial de instalación](https://www.unrealircd.org/docs/Installing_from_source).

### Gestor de módulos (recomendado)

1. Añade el repositorio UDB a `conf/modules.sources.list`:

   ```text
   https://raw.githubusercontent.com/davidlig/unrealircd-udb/main/modules.list
   ```

2. Desde el directorio donde está instalado UnrealIRCd, instala el módulo:

   ```bash
   ./unrealircd module install third/udb
   ```

### Compilación desde el código fuente

Desde el directorio del código fuente de UnrealIRCd:

```bash
git clone https://github.com/davidlig/unrealircd-udb.git src/modules/third/udb
make custommodule MODULEFILE=udb/src/udb
ln -sf udb/src/udb.so src/modules/third/udb.so
make install
```

### Configuración mínima

Añade lo siguiente a `conf/unrealircd.conf`, sustituyendo el nombre del
propagador por el servidor de autoridad directamente conectado de tu red:

```conf
loadmodule "third/udb";

require module {
    name "third/udb";
};

udb {
    propagator "services.example.net";
};
```

Comprueba la configuración y reinicia el servidor:

```bash
./unrealircd configtest
./unrealircd restart
```

Las topologías sin propagador local y todas las opciones avanzadas se explican
en la documentación técnica.

## Documentación técnica

- [Especificación técnica y protocolo (Español)](doc/udb_technical_es.md)
- [Technical specification and protocol (English)](doc/udb_technical_en.md)

## Servidor de pruebas

UDB puede probarse en la red oficial de desarrollo:

- Servidor TLS: [irc.davidlig.net:6697](ircs://irc.davidlig.net:6697)
- Servicios: [Ares IRC Services](https://github.com/davidlig/ares-irc-services)

## Créditos

- Autor y desarrollador principal: **David Abuín Fontán (`davidlig`)**
- Concepto original de UDB: **Trocotronic**
- Proyecto: [github.com/davidlig/unrealircd-udb](https://github.com/davidlig/unrealircd-udb)
