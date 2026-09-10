# UDB 4 para UnrealIRCd 6

UDB (Unreal DataBase) es un módulo global para **UnrealIRCd 6** que mantiene una base distribuida de nicks, canales, políticas por IP, ajustes, opciones por servidor y sanciones, con persistencia atómica, reconciliación por snapshots y control explícito de autoridad.

**Versión actual del módulo:** `4.0.0`  
**Compatibilidad declarada:** UnrealIRCd `6.2.*`  
**Módulo:** `third/udb`  
**Licencia:** GPL v2 o posterior

Esta documentación se ha reconstruido desde el código de `main` en el commit `75d017117d934f9dcb64dbeabe99d1888b72dcab` (10-09-2026).

## Qué gestiona

| Bloque | Contenido |
|---|---|
| `N` | Nicks: contraseña, acceso CIDR, vhost, operclass, modos, snomasks, SWHOIS, forbid/suspended |
| `C` | Canales: founder, modos, topic, access, contraseña, forbid/suspended y opciones |
| `I` | IP/realhost: clones, excepciones `nolines`, host/vhost explícito |
| `S` | Ajustes globales: clones, flood, servicios, cifrado/sufijo y propagador |
| `L` | Opciones por servidor, actualmente `DEBUG` |
| `K` | G-Line, Z-Line, Shun, Q-Line y Spamfilter |

UDB **no es un NickServ/ChanServ de registro interactivo**. Normalmente una autoridad/Services modifica la base por el protocolo S2S `DB`; cada IRCd valida, persiste y aplica esos cambios.

## Características principales

- Seis bloques persistentes con validación de esquema estricta.
- Mutaciones copy-on-write: persistencia antes de publicar el nuevo estado runtime.
- Snapshots atómicos mediante fichero temporal, `fsync`, `rename` y `fsync` del directorio.
- Estado durable `.udb_state`: un READY sólo es válido cuando los seis bloques pertenecen a la misma generación.
- Protocolo **HEL 4** obligatorio entre peers UDB, con capacidad **OCL** obligatoria.
- Reconciliación `INF → RES → BEGIN/PUT/END → ACK` con checksum, límites y timeouts de inactividad/absoluto.
- Autoridad/propagador seleccionada únicamente entre peers directamente enlazados.
- Failover ordenado mediante `S::propagator`.
- Estado `READY/BOOTSTRAPPING` separado de salud `OK/DEGRADED/STALE`.
- Fail-closed: un nodo NOT_READY rechaza nuevos clientes locales hasta recuperar una base durable.
- Registro distribuido de operclasses **OCL** y vista global consistente **OCLG**.
- Propagación multihop de mutaciones, manteniendo los snapshots de reconciliación hop-by-hop.

## Instalación

El repositorio distribuye un bundle único en:

```text
dist/udb.c
```

La metadata de `modules.list` declara `third/udb` para UnrealIRCd `6.2.*`. Tras instalar/compilar el módulo con el mecanismo de módulos de UnrealIRCd, la configuración debe cargarlo:

```text
loadmodule "third/udb";
```

Configuración mínima habitual cuando la base debe obtenerse de Services/otro peer:

```text
udb {
    propagator "ares-services.example.net";
};
```

Después valida y reinicia UnrealIRCd:

```bash
./unrealircd configtest
./unrealircd restart
```

Si no hay ninguna política de propagador, un directorio realmente nuevo se inicializa como autoridad **standalone** con una base vacía READY. Una vez READY y sin política, ese nodo no acepta imports remotos.

## Configuración

```text
udb {
    database-directory "/ruta/local";
    propagator "ares-services.example.net";
    max-global-clones 0;
    password-flood "5:60";
    max-staged-records 500000;
    max-staged-bytes 67108864;
    sync-inactivity-timeout 60;
    sync-absolute-timeout 300;
    stale-timeout 300;
};
```

| Directiva | Default | Observación |
|---|---:|---|
| `database-directory` | `PERMDATADIR` | Directorio de `udb_[NCISLK].db` y `.udb_state` |
| `propagator` | — | Un único nombre de servidor en configuración local |
| `max-global-clones` | 0 | **Actualmente se parsea pero no se usa en runtime** |
| `password-flood` | `5:60` | Intentos fallidos por perfil/IP y ventana temporal |
| `max-staged-records` | 500000 | Máximo por snapshot entrante |
| `max-staged-bytes` | 64 MiB | Máximo acumulado de payload staged |
| `sync-inactivity-timeout` | 60 s | Se renueva con actividad PUT |
| `sync-absolute-timeout` | 300 s | No se renueva con actividad |
| `stale-timeout` | 300 s | Tiempo NOT_READY antes de STALE |

`S::propagator`, a diferencia de la directiva local, puede contener una lista ordenada:

```text
udb-a.example.net,udb-b.example.net,udb-c.example.net
```

La configuración local tiene precedencia sobre el valor distribuido de S.

## Persistencia

En el directorio configurado se mantienen:

```text
udb_N.db
udb_C.db
udb_I.db
udb_S.db
udb_L.db
udb_K.db
.udb_state
```

Los seis snapshots forman un único conjunto READY. Al arrancar, UDB no publica un READY persistido si falta un bloque, existe corrupción, la generación no coincide o quedan backups `.udb_previous` de una operación incompleta.

No edites estos ficheros con el daemon activo. Las mutaciones deben entrar por el protocolo autorizado para conservar esquema, checksums, atomicidad y efectos runtime.

## Protocolo y sincronización

Un enlace directo negocia:

```text
DB <peer> HEL 4 <selector> <epoch> OCL [OCLG]
```

Sin HEL 4/OCL confirmado no se acepta el resto del protocolo; un timeout de HEL puede provocar que UDB aborte el enlace.

La reconciliación compara los seis bloques mediante `INF`. Sólo los bloques divergentes se solicitan con `RES` y se reciben en un árbol privado con `BEGIN/PUT/END`. El `END` valida el checksum, persiste el snapshot y sólo después publica el nuevo árbol.

Las mutaciones en vivo son `INS`, `DEL`, `DRP` y `OPT`. Pueden retransmitirse multihop; las transferencias staged no se retransmiten.

Consulta [doc/udb_technical_es.md](doc/udb_technical_es.md) para la gramática completa y las invariantes de autoridad.

## Uso por usuarios

Para un nick registrado:

```text
/NICK alice:Password
```

Si el nick está ocupado y se quiere recuperar:

```text
/NICK alice!Password
/GHOST alice Password
```

Si `N::access` existe, también debe coincidir la IP con sus CIDR autorizados.

En un canal registrado con `pass`, la contraseña se utiliza como key del JOIN:

```text
/JOIN #canal Password
```

Una autenticación correcta puede conceder `+a`. El fundador identificado recibe `+q`. La extensión:

```text
/INVITE nick #canal Password
```

valida la contraseña del canal y crea, para un target local, una autorización temporal de invitación.

## Diagnóstico de operador

```text
/UDB STATUS
/UDB OPERCLASSES [filtro]
/UDB OPERCLASS <nombre>
/DBQ <block>[::path]
/DBQ <server> <block>[::path]
```

`/UDB STATUS` es la primera comprobación recomendada: muestra readiness, salud, recuperación, propagador seleccionado, política, disponibilidad y admisión de clientes.

`/UDB OPERCLASSES` verifica si todos los IRCd participantes han entregado inventario OCL. `/UDB OPERCLASS <nombre>` comprueba que una operclass exista y tenga el mismo digest efectivo en todos.

### Advertencia actual de DBQ

El código redacciona `N::*::pass`, `N::*::challenge` y `S::encryption_key`, pero **todavía no redacciona `C::*::pass` ni `C::*::challenge`**. Hasta que se corrija, considera `DBQ` una interfaz privilegiada que puede exponer credenciales de canal a opers autorizados.

## Desarrollo

Las fuentes canónicas viven en `src/`. No se debe editar `dist/udb.c` como fuente primaria.

Regenerar bundle:

```bash
python3 scripts/bundle.py
```

Comprobar drift sin modificar ficheros:

```bash
python3 scripts/bundle.py --check
```

El CI actual compila contra UnrealIRCd 6.2.6, valida el bundle y ejecuta suites normales y con ASan/UBSan para persistencia, bootstrap, sincronización, multihop, failover, OCL, límites y efectos runtime.

## Documentación

- [Documentación técnica en español](doc/udb_technical_es.md)
- [Technical documentation in English](doc/udb_technical_en.md)
- [README in English](README.md)
