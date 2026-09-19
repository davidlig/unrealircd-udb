# UDB 4 para UnrealIRCd 6

UDB (Unreal DataBase) es un módulo global para **UnrealIRCd 6** que mantiene una base distribuida de nicks, canales, políticas por IP, ajustes, opciones por servidor y sanciones, con persistencia atómica, reconciliación por snapshots y control explícito de autoridad.

**Versión actual del módulo:** `4.0.0`
**Compatibilidad declarada:** UnrealIRCd `6.2.*`
**Módulo:** `third/udb`
**Licencia:** GPL v2 o posterior

## Qué gestiona

| Bloque | Contenido |
|---|---|
| `N` | Nicks: contraseña, acceso CIDR, vhost, operclass, modos, snomasks, SWHOIS, forbid/suspend |
| `C` | Canales: founder, modos, topic, access, forbid/suspend y opciones |
| `I` | IP/realhost: clones, excepciones `nolines`, host/vhost explícito |
| `S` | Ajustes globales: clones, flood, servicios, cifrado/sufijo y propagador |
| `L` | Opciones por servidor, actualmente `DEBUG` |
| `K` | G-Line, Z-Line, Shun, Q-Line y Spamfilter |

UDB **no es un NickServ/ChanServ de registro interactivo**. Normalmente una autoridad/Services modifica la base por el protocolo S2S `DB`; cada IRCd valida, persiste y aplica esos cambios. El bloque K usa un único schema de perfil canónico; el contrato exacto de G/Z/S/Q y Spamfilter F dinámico está documentado en la guía técnica.

## Características principales

- Seis bloques persistentes con validación de esquema estricta.
- Mutaciones copy-on-write: persistencia antes de publicar el nuevo estado runtime.
- Snapshots atómicos mediante fichero temporal, `fsync`, `rename` y `fsync` del directorio.
- Estado durable `.udb_state`: un READY sólo es válido cuando los seis bloques pertenecen a la misma generación.
- Protocolo **HEL 4** obligatorio entre peers UDB, con capacidad **OCL** obligatoria.
- Reconciliación `INF → RES → BEGIN/PUT/END → ACK` con manifiestos canónicos de estado SHA-256, límites de staging y timeouts de inactividad/absoluto.
- Freshness autoritativo (Política A): el propagador seleccionado es la autoridad; los timestamps locales del sistema de archivos (`mtime`) nunca anulan el estado de la red ni deciden propiedad de registros.
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
    propagator "ares-services.example.net";
    password-flood "5:60";
    max-staged-records 500000;
    max-staged-bytes 67108864;
    sync-inactivity-timeout 60;
    sync-absolute-timeout 300;
    stale-timeout 300;
    anti-entropy-interval 1800;
};
```

| Directiva | Default | Observación |
|---|---:|---|
| `propagator` | — | Un único nombre de servidor en configuración local |
| `password-flood` | `5:60` | Intentos fallidos por perfil/IP y ventana temporal |
| `max-staged-records` | 500000 | Máximo por snapshot entrante |
| `max-staged-bytes` | 64 MiB | Máximo acumulado de payload staged |
| `sync-inactivity-timeout` | 60 s | Se renueva con actividad PUT |
| `sync-absolute-timeout` | 300 s | No se renueva con actividad |
| `stale-timeout` | 300 s | Tiempo NOT_READY antes de STALE |
| `anti-entropy-interval` | 1800 s | Cadencia de MANIFEST REQ con ±10% de jitter |

`S::propagator`, a diferencia de la directiva local, puede contener una lista ordenada:

```text
udb-a.example.net,udb-b.example.net,udb-c.example.net
```

La configuración local tiene precedencia sobre el valor distribuido de S.

## Persistencia

En `PERMDATADIR` de UnrealIRCd se mantienen:

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

Sin HEL 4/OCL confirmado no se acepta el resto del protocolo; un timeout de HEL puede provocar que UDB aborte el enlace. HEL negocia capacidad y autoridad, pero no autentica criptográficamente al peer: UDB hereda la frontera de confianza del enlace de servidores de UnrealIRCd. Usa enlaces TLS autenticados y con verificación de certificado en cada salto UDB; sin TLS, snapshots, hashes de contraseña, claves de canal y mutaciones quedan expuestos en tránsito.

El bloque `K` usa `expires *<timestamp_unix>` para sanciones temporales G/Z/S/Q/F; sin `expires` es permanente. Los followers retransmiten solicitudes `EXP` dirigidas salto a salto hacia la autoridad raíz seleccionada y nunca borran persistencia autoritativa localmente; sólo esa autoridad elimina el subtree vencido y origina el `DEL` secuenciado.


### Rutas IPv4/IPv6 del bloque K

En una ruta UDB, `::` es el separador de componentes, por lo que cada `:` de IPv6 es `%3A`; `@` y `/` imprimibles permanecen literales. Por ejemplo: `K::Z::2001%3Adb8%3A%3A1::reason`, `K::Z::2001%3Adb8%3A1234%3A%3A/48::reason`, `K::G::*@2001%3Adb8%3A%3A1::reason` y `K::S::*@2001%3Adb8%3A%3A1::reason`. Z exige la grafía canónica de dirección de `inet_ntop()` y la dirección de red CIDR (sin bits de host); G/S mantienen `user@host` y todavía no canonicalizan hosts IPv6. Consulta la guía técnica para ejemplos `DB INS`/`DEL` y la política completa.


La reconciliación compara los seis bloques mediante `INF`. Sólo los bloques divergentes se solicitan con `RES` y se reciben en un árbol privado con `BEGIN/PUT/END`. El `END` valida el checksum, persiste el snapshot y sólo después publica el nuevo árbol.

Las mutaciones en vivo son `INS`, `DEL` y `DRP`; `EXP` es una solicitud dirigida de expiración follower-a-autoridad. Las mutaciones conservan el SID/epoch/secuencia del origen raíz mientras cada relay las valida y retransmite; las transferencias staged siguen siendo salto a salto y nunca se reenvían.

Consulta [doc/udb_technical_es.md](doc/udb_technical_es.md) para la gramática completa y las invariantes de autoridad.

## Uso por usuarios

Sólo un perfil que contiene `N::pass` está protegido por contraseña y puede establecer identidad UDB:

```text
/NICK alice:Password
```

Si un nick protegido por contraseña está ocupado y se quiere recuperar:

```text
/NICK alice!Password
/GHOST alice Password
```

Sin `N::pass`, un perfil no es una identidad: el nick puede usarse si `forbid` y `access` lo permiten, pero no concede `account=<nick>`, `+r`, vhost, oper, modos, snomasks ni SWHOIS. Esos efectos configurados están dormidos: UDB no los aplica ni los utiliza como política negativa para retirar estado equivalente aportado por otra fuente, tampoco cuando se borra un registro dormido o un snapshot completo de N reemplaza o elimina el perfil. `N::access` restringe el uso del nick (y la autenticación cuando existe `pass`); coincidir con él nunca autentica. La sintaxis de contraseña/recovery no prueba propiedad en un perfil sin `pass`.

`N::snomasks` admite expresiones relativas (`+...`, `-...` o mixtas `+c-k`); las snomasks externas se preservan durante la reconciliación exclusiva del perfil. `N::oper` respeta estrictamente los opers externos: UDB nunca reemplaza ni asume la propiedad de una sesión oper preexistente, y la revocación sólo retira opers genuinamente concedidos por UDB sin alterar contadores de oper. Al revocar un oper propiedad de UDB se usa la limpieza nativa de deoper de UnrealIRCd, que elimina todas las snomasks, incluidas las modificadas externamente.

Una credencial `/NICK nick:Password` es de un solo uso y sólo aplica a ese intento: la cuenta, `+r` y los efectos del perfil sólo se materializan cuando el cambio de nick es efectivo, y un cambio rechazado (`433`, Q-line, límites de cambio de nick) deja intacta la identidad UDB activa del nick actual. Una autenticación explícita correcta envía `You are now identified for nickname <nick>.` como NOTICE de NickServ; los refresh internos del perfil no lo repiten. El registro inicial sigue la misma regla, y una credencial de un intento fallido nunca se reutiliza por un rename forzado posterior. Cuando un perfil antes passless recibe su primer `N::pass`, el `account`/`+r` de otra fuente no se acepta como su autenticación. Un cambio de nick forzado por servicios (`SVSNICK`) nunca es autenticación UDB: un nick protegido alcanzado sin una identidad activa válida se renombra de forma segura.

Un nick suspendido protegido por contraseña sigue exigiendo sus comprobaciones normales de `pass`/`access` antes de poder adoptarse. `N::pass` admite los tipos `argon2id`, `sha256` y `crypt`, seleccionados por su propio prefijo (`argon2id:`, `sha256:` o `crypt:`). Usa `argon2id` para credenciales nuevas y migra registros `sha256`/`crypt` heredados cuando los usuarios se autentiquen; mantener compatibilidad no convierte SHA-256 sin salt en una opción adecuada para contraseñas nuevas. Exige TLS a los clientes que envíen `/NICK nick:Password` o `/GHOST`, porque esos comandos transportan la contraseña proporcionada. La autenticación correcta queda ligada únicamente al nick activo: mientras exista `N::suspend`, UDB no publica account/`+r`, no aplica efectos del perfil y no conserva autenticación alguna. Al eliminar `suspend` de un perfil con `pass`, el ocupante actual se renombra y recibe los avisos de NickServ `The nickname <nick> is no longer suspended.` y después `You have been renamed. If you are the owner, please identify: /NICK <nick>:Password`; debe autenticarse de nuevo, mientras que un perfil sin `pass` no tiene identidad que revocar y mantiene a su ocupante. Account/`+r` por sí solos no pueden crear identidad. UDB no añade ni retira el `+S` de propiedad externa.

La clave de canal es exclusivamente el parámetro nativo `+k` de `C::<canal>::modes`, por ejemplo `+ntk secret`. Protege tanto el primer JOIN como los posteriores. `C::<canal>::modes` rechaza el marcador de registro `r`, los rangos de miembro (`q`, `a`, `o`, `h`, `v`) y `P`; la persistencia se expresa sólo con el bit `PERSISTENT` de `C::options`. El `+r` nativo es estado de canal registrado propiedad de UDB: se aplica al registrar el canal aunque ya exista, se retira mientras exista `C::<canal>::suspend` o se elimine el perfil, y se restaura junto con el `+q` del fundador al levantar la suspensión. Un fundador identificado recibe `+q` de UDB. Mientras existe `C::<canal>::suspend`, cada usuario local que entra al canal recibe un aviso de ChanServ con el valor almacenado como motivo de la suspensión.

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

### Redacción de secretos

El código redacciona `N::*::pass`, `S::encryption_key` y el valor completo de `C::<canal>::modes`, ya que puede contener una clave nativa `+k`. Los avisos de debug de modos también redaccionan los parámetros cuando la expresión de modos contiene `k`.

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
