# UDB 4 — Documentación técnica

> Documento reconstruido desde la implementación actual de `davidlig/unrealircd-udb`, rama `main`, commit `75d017117d934f9dcb64dbeabe99d1888b72dcab`, revisado el 10 de septiembre de 2026. El código, no las versiones anteriores de esta documentación, se ha utilizado como fuente de verdad.

## 1. Alcance

UDB (Unreal DataBase) es un módulo global para UnrealIRCd 6 que mantiene y aplica una base de datos distribuida para registros de nick, canales, políticas por IP, ajustes, opciones por servidor y sanciones. La versión del módulo es **4.0.0** y el metadato de distribución declara **UnrealIRCd 6.2.x** como versión mínima (`min-unrealircd-version "6.2.*"`).

La implementación canónica está dividida en `src/` y se amalgama de forma determinista en `dist/udb.c`. El módulo se registra como `third/udb` y utiliza el comando S2S `DB` como protocolo propio.

UDB no implementa un servicio de registro autónomo para crear cuentas o canales desde IRC. Las escrituras autorizadas llegan por el protocolo `DB` desde la autoridad seleccionada; el IRCd aplica esas mutaciones, las persiste y las retransmite cuando corresponde.

## 2. Arquitectura

`src/udb.c` compone una única unidad de compilación en este orden:

1. `udb_store.c.inc`: árbol de registros, rutas y persistencia.
2. `udb_config.c.inc`: `udb {}`, bloque S y opciones L.
3. `udb_core.c.inc`: validación, esquemas, checksums y operaciones del árbol.
4. `udb_services.c.inc`: resolución de fuentes NickServ/ChanServ/IpServ.
5. `udb_effects.c.inc`: aplicación y retirada de efectos en runtime.
6. `udb_sync.c.inc`: HEL, autoridad, reconciliación y snapshots staged.
7. `udb_operclasses.c.inc`: inventarios OCL y vista global OCLG.
8. `udb_mutation.c.inc`: `INS`, `DEL`, `DRP` y `OPT`.
9. `udb_protocol.c.inc`: parser y encaminamiento del comando `DB`.
10. `udb_nicks.c.inc`, `udb_channels.c.inc`, `udb_ips.c.inc`, `udb_lines.c.inc`: comportamiento específico por bloque.
11. `udb_query.c.inc`: comandos de diagnóstico `DBQ` y `UDB`.
12. `udb_lifecycle.c.inc`: arranque, publicación, apagado y estado durable.

El estado lógico se separa en seis bloques independientes, pero **READY es una propiedad del conjunto completo**, no de un bloque individual.

| Bloque | Nombre | Función principal |
|---|---|---|
| `N` | Nicks | Registro, autenticación y atributos de nicks |
| `C` | Channels | Canales registrados, acceso y políticas de canal |
| `I` | IPs | Clones, excepciones y hosts por IP/realhost |
| `S` | Settings | Ajustes globales y selección de propagador |
| `L` | Links | Opciones locales por nombre de servidor |
| `K` | Lines | G/Z/Shun/Q y spamfilters |

## 3. Modelo de datos y rutas

Cada bloque mantiene un árbol `UdbRecord`. Los componentes de ruta se separan con `::` y se codifican de forma canónica cuando contienen bytes reservados. El encoder convierte `:`, `%`, bytes de control/espacio (`<= 0x20`) y bytes no ASCII (`>= 0x7f`) a `%HH` en hexadecimal mayúscula.

Ejemplos conceptuales:

```text
N::alice::pass
C::#chat::founder
C::#chat::access::alice
I::203.0.113.20::clones
S::propagator
K::F::<pattern>::action
```

En los ficheros de bloque se omite la letra, ya que ésta está implícita en el fichero:

```text
alice::pass sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
alice::modes +iw
```

Los valores numéricos se serializan como `*<entero>`; los valores de texto se escriben literalmente después del primer espacio. El parser numérico es estricto: no admite signo, espacios ni caracteres sobrantes.

### 3.1 Límites de representación

La implementación aplica, entre otros, estos límites:

- ruta lógica codificada: **8192 bytes**;
- componente sin codificar: **4608 bytes**;
- componente codificado: **4608 bytes**;
- valor de registro: **4096 bytes**;
- `txid` de snapshot: **31 caracteres**;
- patrón de spamfilter decodificado: **3072 bytes**;
- cada registro debe caber también en una línea S2S de UnrealIRCd dejando un margen interno de 256 bytes;
- hash de primer nivel: 2048 slots por bloque.

No basta con que una ruta quepa en disco: debe cumplir además el límite S2S para poder ser sincronizada de forma segura.

## 4. Esquema de los seis bloques

### 4.1 Bloque N — Nicks

Formato general:

```text
N::<nick>::<clave>
```

Claves admitidas:

| Clave | Tipo | Efecto actual |
|---|---|---|
| `access` | texto | Lista de CIDR separada por comas/espacios desde la que se permite usar el nick. Si falta, no restringe por IP. |
| `pass` | texto | Hash de contraseña. |
| `challenge` | texto | Fuerza el tipo de autenticación: `argon2id`, `sha256` o `crypt`. |
| `vhost` | texto | Vhost aplicado al usuario identificado. |
| `forbid` | texto | Impide el uso del nick; en hot-sync puede forzar renombre. |
| `suspended` | texto | Marca el usuario identificado con modo `+S`. |
| `oper` | texto | Nombre de operclass local a conceder. |
| `modes` | texto | Modos de usuario válidos; `o` está expresamente prohibido aquí. |
| `snomasks` | texto | Snomasks a aplicar. |
| `swhois` | texto | SWHOIS administrado por UDB. |

Contraseñas aceptadas:

```text
argon2id:$argon2id$...
sha256:<64 hex>
crypt:<hash>
```

También se reconoce un `$argon2id$...` sin prefijo si no hay `challenge`. En SHA-256 se compara el SHA-256 hexadecimal de la contraseña enviada.

El control de fallos de contraseña usa una tabla de 256 entradas indexada conceptualmente por perfil/IP. El valor por defecto es `5:60` y puede configurarse con `udb::password-flood` o sustituirse en runtime mediante `S::flood`.

#### Uso del nick

Para un nick registrado:

```text
/NICK alice:Password
```

autentica el cambio normal. Si el nick está ocupado:

```text
/NICK alice!Password
```

valida contraseña + `access` y expulsa al ocupante antes de tomar el nick. También existe:

```text
/GHOST alice Password
```

Si el perfil contiene `access`, la contraseña correcta **no basta**: la IP del cliente debe coincidir con al menos uno de los CIDR configurados.

Al identificar correctamente el nick, UDB asigna `account=<nick>` y `+r`; después aplica suspensión, vhost, operclass, modos, SWHOIS y snomasks. Cuando el usuario abandona el perfil, UDB retira el estado que posee, incluido el oper concedido por UDB.

En un reemplazo caliente del bloque N no se confía únicamente en `+r`: la cuenta actual debe coincidir con el perfil. De lo contrario no se aplican vhosts/opers del perfil y, si existe contraseña, el ocupante puede ser renombrado.

### 4.2 Bloque C — Canales

Formato:

```text
C::<canal>::<clave>
C::<canal>::access::<nick>
```

Claves:

| Clave | Tipo | Efecto actual |
|---|---|---|
| `founder` | nick | Fundador identificado. Recibe `+q` gestionado por UDB. |
| `modes` | texto | Modos de canal y parámetros validados contra los handlers cargados. |
| `topic` | texto | Topic gestionado por UDB. |
| `access` | contenedor | Lista de nicks autorizados. |
| `forbid` | texto | Rechaza el JOIN con el motivo almacenado. |
| `suspended` | texto | Suprime el comportamiento de canal registrado/fundador. |
| `pass` | texto | Contraseña de administración del canal. |
| `challenge` | texto | Tipo de hash de la contraseña. |
| `options` | numérico | Máscara de bits descrita abajo. |

`C::<canal>::access::<nick>` acepta valor numérico o de texto por esquema, pero **el hook de JOIN actual sólo comprueba la existencia de la entrada y que el usuario tenga `+r`**. El valor de la entrada no se interpreta como rango en esta implementación.

Opciones (`options`):

| Bit | Valor | Nombre | Comportamiento |
|---|---:|---|---|
| `0x01` | 1 | `PROTECT_BANS` | Un usuario normal no puede retirar un ban local creado por otro usuario; fundador y oper quedan exentos. |
| `0x02` | 2 | `LOCK_MODES` | Bloquea cambios locales de modos excepto listas `b`, `e`, `I`. |
| `0x04` | 4 | `LOCK_TOPIC` | Bloquea cambios locales de topic. |
| `0x08` | 8 | `PERSISTENT` | Aplica `+P` si el modo nativo `P` existe; UDB no lo emula si falta. |

Los bits pueden combinarse. Por ejemplo `*15` habilita los cuatro.

#### JOIN, fundador y contraseña

El fundador sólo se considera identificado si su nick coincide con `founder` y tiene `+r`. Puede saltarse bans/keys/invite de JOIN y recibe `+q` salvo que el perfil esté suspendido.

Si existe `pass`, un usuario no fundador debe usar la contraseña como key de JOIN:

```text
/JOIN #canal Password
```

Tras una autenticación correcta, UDB marca la autorización y concede `+a` después del JOIN. La contraseña comparte el mismo verificador de hashes que N.

`INVITE` dispone de una extensión cuando el canal tiene `pass`:

```text
/INVITE nick #canal Password
```

La contraseña debe ser válida y el destinatario debe ser local al servidor que procesa el comando. Si la invitación se acepta, se crea un grant temporal de 300 segundos que puede permitir el JOIN sin volver a presentar la contraseña. Si el propio usuario presenta contraseña en JOIN, se considera autenticación de administrador y puede recibir `+a`.

Los bans protegidos se rastrean en memoria por canal/ban/propietario. Ese propietario es estado runtime, no un registro persistente del bloque C.

### 4.3 Bloque I — IPs

Formato:

```text
I::<ip-o-realhost>::clones *N
I::<ip-o-realhost>::nolines <flags>
I::<ip-o-realhost>::host <vhost>
```

Claves:

- `clones`: máximo de conexiones simultáneas para esa IP; `0` no se usa como límite efectivo.
- `nolines`: cadena de hasta 16 caracteres formada por `GZQSTmc`; UDB crea una excepción TKL propia. La `c` activa además la exención explícita del connect-flood en el hook de pre-connect.
- `host`: override explícito de host/vhost para clientes coincidentes.

Aunque el validador de la clave raíz admite un texto de host/IP válido, **la búsqueda runtime de este bloque es exacta** contra `client->ip` y, como alternativa, `realhost`; no implementa matching CIDR de la clave I.

Para clones, primero se intenta `I::<ip>::clones`. Si no hay límite específico, el código consulta `S::clones`. El contador recorre usuarios conectados con la misma IP y rechaza cuando ya se ha alcanzado el límite; el texto por defecto es `Too many connections from your IP` o `S::quit_clones` si está configurado.

#### Vhost derivado

Si existen simultáneamente `S::encryption_key` y `S::suffix`, UDB puede derivar un vhost estable con:

```text
HMAC-SHA256(key, "UDB-vhost-v1|<ip>|<realhost>")
```

Se usan los primeros 16 bytes del HMAC como 32 caracteres hexadecimales y se concatena `suffix`. `encryption_key` debe contener exactamente 64 hex y `suffix` debe empezar por `.` y cumplir las restricciones de hostname.

Un `I::host` es un override explícito y bloquea el reemplazo por el vhost derivado mientras siga siendo el estado IP activo. UDB conserva el host anterior para poder restaurarlo al retirar su efecto.

### 4.4 Bloque S — Settings

El bloque S sólo admite claves de profundidad 1:

| Clave | Tipo | Uso actual |
|---|---|---|
| `clones` | numérico | Límite global de clones usado como fallback por el bloque I. |
| `quit_ips` | texto | Se almacena en el contexto; **no hay un consumidor de desconexión por límite IP en el código actual**. |
| `quit_clones` | texto | Mensaje al rechazar por clones. |
| `flood` | texto `intentos:segundos` | Sobrescribe el límite de fallos de contraseña; al borrarlo vuelve al valor local. |
| `encryption_key` | 64 hex | Clave HMAC para vhosts derivados. |
| `suffix` | hostname empezando `.` | Sufijo del vhost derivado. |
| `nickserv` | máscara `nick!user@host` | Fuente preferida para notices NickServ. |
| `chanserv` | máscara `nick!user@host` | Fuente preferida para acciones/notices ChanServ. |
| `ipserv` | máscara `nick!user@host` | Fuente preferida para notices de IP/vhost. |
| `propagator` | lista de servidores | Política distribuida de autoridad/failover. |

Las máscaras de servicio sólo seleccionan un usuario **ULine** conectado si existe exactamente una coincidencia. Si no hay coincidencia o hay varias, UDB usa el servidor local como fuente efectiva y emite log de fallback.

`S::propagator` puede ser una lista ordenada separada por comas, por ejemplo:

```text
S::propagator udb-a.example.net,udb-b.example.net,udb-c.example.net
```

El primer candidato utilizable gana.

### 4.5 Bloque L — Links

Formato:

```text
L::<servername>::options *N
```

Sólo existe el bit:

```text
0x01 = DEBUG
```

El modo debug local se consulta específicamente en `L::<me.name>::options`. Al activarlo se habilita el flujo de debug de UDB y se retira el filtro que normalmente evita duplicar logs UDB hacia destinos snomask/oper.

Cualquier bit desconocido hace que el efecto se ignore con warning.

### 4.6 Bloque K — Lines

Tipos raíz admitidos:

- `G`: G-Line / server ban global.
- `Z`: Z-Line global.
- `S`: Shun global.
- `Q`: name/Q-Line global.
- `F`: spamfilter global.

Para G/Z/S/Q se admite:

```text
K::<tipo>::<patron> <reason>
K::<tipo>::<patron>::reason <reason>
K::<tipo>::<patron>::expires *<timestamp_unix>
```

El reason puede estar directamente en el nodo de patrón o en `::reason`. `expires` es un único timestamp Unix absoluto elegido por el origen; su ausencia es la única representación de una línea permanente. `expires *0` es inválido. Un timestamp igual o anterior a la hora local nunca se materializa como TKL.

La expiración no se deriva del estado runtime de la TKL. La autoridad barre perfiles K vencidos y realiza el `DEL K::<tipo>::<patron>` transaccional canónico, que elimina el subtree completo de memoria y de `udb_K.db`; los followers sólo eliminan su TKL runtime local y envían una solicitud compare-and-delete `EXP <path> <expected-expires>`. Por tanto restart, reload, snapshot, reconnect o un cambio de `reason`, `type` o `action` no pueden renovar ni resucitar una línea temporal. Sólo un `INS ...::expires` explícito cambia su vida; `DEL ...::expires` convierte un perfil restante válido en permanente.

UDB etiqueta sus TKL con `set_by="UDB"` y sólo elimina/reemplaza líneas que reconoce como propias para el mismo tipo/patrón.

Para spamfilter F la profundidad es obligatoriamente 3 y se utilizan estas propiedades:

```text
K::F::<patron>::type <targets>
K::F::<patron>::action <action>
K::F::<patron>::expires *<timestamp_unix>
K::F::<patron>::reason <texto>
```

`type` se valida con los targets nativos de UnrealIRCd y `action` con su parser de ban actions; se rechazan acciones config-only. El patrón debe compilar como PCRE antes de persistirse. Puede almacenarse en claro o como Base64 canónico con prefijo `b64:`; el patrón decodificado no puede superar 3072 bytes ni contener NUL.

## 5. Configuración `udb {}`

Configuración mínima típica:

```text
loadmodule "third/udb";

udb {
    propagator "ares-services.example.net";
};
```

Directivas admitidas:

| Directiva | Rango / formato | Default |
|---|---|---|
| `database-directory` | ruta local, sin `://`, sin CR/LF | `PERMDATADIR` de UnrealIRCd |
| `propagator` | un único nombre de servidor válido | no definido |
| `max-global-clones` | 0..1,000,000 | 0 |
| `password-flood` | `intentos:segundos`, ambos > 0 | `5:60` |
| `max-staged-records` | 1..10,000,000 | 500,000 |
| `max-staged-bytes` | 1,024..1,073,741,824 bytes | 64 MiB |
| `sync-inactivity-timeout` | 1..86,400 s | 60 s |
| `sync-absolute-timeout` | 1..86,400 s | 300 s |
| `stale-timeout` | 1..604,800 s | 300 s |

Las directivas desconocidas son error de configuración.

### 5.1 Precedencia de propagador

La política se resuelve en este orden:

1. `udb::propagator` del `unrealircd.conf` local.
2. `S::propagator` de la base ya publicada.
3. durante arranque, el candidato `S::propagator` cargado pero todavía no publicado.
4. ausencia de política.

Hay una diferencia deliberada: `udb::propagator` valida **un servidor**, mientras `S::propagator` admite una **lista ordenada** para failover.

Un candidato remoto sólo es seleccionable si es un servidor **directamente enlazado** al nodo actual; cuando se exige disponibilidad de protocolo, también debe tener `HEL 4` confirmado. Un servidor no adyacente no se convierte en fuente de snapshots de ese nodo aunque aparezca en la topología global.

### 5.2 Nota de implementación: `max-global-clones`

La directiva `max-global-clones` se valida y se guarda en `UdbConfig.max_global_clones`, pero el código actual no lee ese campo fuera de la configuración. El fallback de clones que sí se ejecuta está implementado mediante `S::clones`.

Por tanto, no debe documentarse `max-global-clones` como un límite efectivo hasta que exista un consumidor runtime o se conecte explícitamente al hook de clones.

## 6. Persistencia y atomicidad

Cada bloque se guarda como:

```text
<database-directory>/udb_N.db
<database-directory>/udb_C.db
<database-directory>/udb_I.db
<database-directory>/udb_S.db
<database-directory>/udb_L.db
<database-directory>/udb_K.db
```

Permisos de creación: `0600`. El directorio se crea con `0700` si no existe.

Cabecera de snapshot:

```text
; UDB Block N - Version 1
; Generation: 12345
; Saved: 1780000000
; Records: 42
```

El checksum no incluye estas cabeceras ni el orden de las líneas. Se calcula como CRC32 sobre las líneas lógicas `ruta valor\n`, ordenadas lexicográficamente. Un árbol vacío tiene checksum `0`.

### 6.1 Commit de un bloque

La escritura de snapshot usa:

1. `open(<fichero>.tmp, O_CREAT|O_EXCL, 0600)` y `O_NOFOLLOW` si existe.
2. serialización completa.
3. `fflush()`.
4. `fsync()` del fichero.
5. `rename(.tmp, fichero)` como punto visible de commit.
6. `fsync()` del directorio padre.

Si falla antes del `rename`, el estado activo no debe publicarse. Si el `rename` ya fue visible pero falla el `fsync`/close del directorio, el resultado se considera **COMMITTED_DURABILITY_UNCERTAIN**: la memoria se mantiene alineada con el fichero visible, pero UDB fuerza `BOOTSTRAPPING` y recuperación.

Las mutaciones `INS`/`DEL` usan copy-on-write: clonan el árbol, aplican/validan la modificación, escriben el snapshot y sólo después sustituyen el árbol activo y sus efectos.

### 6.2 READY como conjunto generacional

Al entrar en READY, los seis bloques se guardan con una misma generación. Antes de reemplazarlos, los ficheros previos se renombran temporalmente a `.udb_previous`. Si cualquiera de los seis falla, se intenta restaurar el conjunto anterior completo.

Después se publica `.udb_state` de forma atómica:

```text
FORMAT=1
STATE=READY
ORIGIN=FRESH
GENERATION=12345
LAST_SYNC=1780000000
```

Estados persistentes: `READY` y `BOOTSTRAPPING`. Orígenes: `FRESH` y `RECOVERY`.

En arranque, un `READY` persistido sólo se acepta si:

- `.udb_state` es sintácticamente válido;
- su generación es distinta de cero;
- existen y parsean correctamente los seis snapshots;
- los seis snapshots tienen la misma generación indicada por `.udb_state`;
- no queda ningún `.udb_previous` de una publicación incompleta.

Snapshots existentes sin `.udb_state`, un estado corrupto, una generación incompleta o backups pendientes **no se publican parcialmente**. UDB mantiene raíces activas vacías y queda NOT_READY/BOOTSTRAPPING hasta recuperar una base autoritativa.

En un directorio realmente nuevo, si no existe política de propagador (modo autoridad standalone) o el primario configurado es el propio servidor, UDB puede persistir los seis bloques vacíos y entrar en READY localmente.

## 7. Negociación HEL 4

Todo peer directo UDB negocia antes de aceptar el resto del protocolo. `HEL` es el único frame `DB` permitido antes de confirmar capacidad.

Solicitud:

```text
:<sid> DB <peer-sid> HEL 4 <selector> <epoch> OCL [OCLG]
```

ACK:

```text
:<sid> DB <peer-sid> HEL 4 ACK <selector> <epoch> OCL [OCLG]
```

- versión exigida: `4`;
- `epoch`: 16 caracteres hexadecimales minúsculos que identifican la instancia OCL;
- `OCL`: obligatorio;
- `OCLG`: opcional; indica suscripción a la proyección global, normalmente usada por consumidores como Services.

El selector anunciado significa:

- `?`: nodo todavía sin política/READY y dispuesto a que ese vecino sea su dueño exclusivo de bootstrap;
- `-`: existe política pero no hay candidato utilizable;
- `<servername>`: fuente/propagador seleccionado.

Si un peer directo no responde al HEL dentro del timeout o no soporta la capacidad OCL requerida, UDB aborta el enlace de servidor. Un cambio de epoch del mismo SID se trata como una nueva instancia y reinicia los latches/replays asociados.

## 8. Modelo de autoridad y bootstrap

### 8.1 Con política

El primer candidato válido de la política se selecciona. Para importar snapshots o aceptar mutaciones, el origen debe coincidir con el peer directo seleccionado y tener HEL confirmado.

Si cambia la política, UDB cancela sesiones/peticiones pertenecientes a otra fuente y vuelve a evaluar la reconciliación.

### 8.2 Sin política

- Antes de READY, el primer peer directo autorizado que actúa como fuente de bootstrap se fija como **bootstrap owner exclusivo**. Otro HEL posterior no puede robárselo.
- Tras READY, el nodo sin política se considera **autoridad standalone** y deja de aceptar imports remotos.

Esto evita que un nodo ya autoritativo adopte accidentalmente la base de un vecino sólo por estar conectado.

## 9. Reconciliación de snapshots

La reconciliación es **pull dirigida por inventario** y hop-by-hop. Los frames de reconciliación no se reenvían a través de la red.

Secuencia para cada ronda:

```text
Autoridad                         Receptor
    |                                |
    | INF round N checksum mtime     |
    |------------------------------->|
    |                                | compara checksum
    |            RES round N         | si diverge
    |<-------------------------------|
    | BEGIN round N txid checksum    |
    |------------------------------->|
    | PUT round N txid path :value   |
    |------------------------------->|
    | ...                            |
    | END round N txid checksum      |
    |------------------------------->|
    |     ACK round N txid digest    |
    |<-------------------------------|
```

La autoridad ofrece `INF` para los **seis bloques**. El receptor sólo puede pasar a READY cuando:

1. ha comparado N/C/I/S/L/K en la ronda;
2. cada bloque divergente ha finalizado su snapshot;
3. no quedan sesiones staged ni `RES` pendientes;
4. puede guardar de forma durable el conjunto READY y `.udb_state`.

### 9.1 Frames

```text
INF   <round> <block> <checksum> <modified_at>
RES   <round> <block>
BEGIN <round> <block> <txid> <checksum>
PUT   <round> <block> <txid> <path> :<string>
PUT   <round> <block> <txid> <path> *<number>
END   <round> <block> <txid> <checksum>
ACK   <round> <block> <txid> <digest>
ERR   <subcmd> <code> <round/correlation> <block>
```

Los IDs de ronda válidos son enteros decimales no cero. `txid` sólo admite alfanuméricos, `-` y `_` y tiene máximo 31 caracteres.

`BEGIN` sólo se acepta si existe una reconciliación activa con la misma autoridad/ronda y ese bloque tenía un `RES` pendiente. `PUT` debe coincidir exactamente con peer/ronda/txid. Una secuencia inválida puede abortar la sesión y la ronda.

`END` recalcula el checksum del árbol staged y exige que coincida con el digest recibido antes de persistir y hacer commit.

### 9.2 Límites staged

Defaults:

- 500.000 registros staged;
- 64 MiB acumulados de `len(path)+len(data)`;
- 60 s de inactividad;
- 300 s de timeout absoluto.

El timeout de inactividad se renueva con cada PUT; el absoluto no. También existen timeout de inventario/reconciliación y timeout de un `RES` que no recibe `BEGIN`.

Las rondas fallidas programan reintentos limitados (`UDB_RECONCILE_RETRY_MAX = 6`) con backoff.

## 10. Mutaciones en vivo

Las mutaciones autorizadas son:

```text
INS <Block::path> <value>
DEL <Block::path>
DRP <block>
OPT <block> [modified_at]
```

Semántica:

- `INS`: inserta o sustituye un valor después de validar límites y esquema.
- `DEL`: elimina una ruta; borrar una ruta inexistente es idempotente.
- `DRP`: vacía un bloque completo, persistiendo primero el snapshot vacío.
- `OPT`: fuerza guardado/actualización del bloque y puede propagar `modified_at`.

Una mutación sólo se acepta desde el propagador remoto seleccionado. Se persiste antes de publicar los efectos runtime. Después de procesarla puede retransmitirse hop-by-hop a peers directos con HEL confirmado, excluyendo la dirección de entrada.

A diferencia de `INF/RES/BEGIN/PUT/END`, las mutaciones sí están diseñadas para propagarse por múltiples saltos mediante retransmisión validada en cada nodo.

Códigos de error S2S:

| Código | Nombre |
|---:|---|
| 1 | `NO_BLOCK` |
| 2 | `PARAMS` |
| 3 | `FATAL` |
| 4 | `SYNC_ACTIVE` |
| 5 | `NO_SYNC` |
| 6 | `FORBIDDEN` |

## 11. Readiness, salud y admisión de clientes

Son conceptos distintos:

- `udb_ready`: base publicable/usable (`READY` o `BOOTSTRAPPING`).
- `udb_sync_status`: salud de sincronización (`OK`, `DEGRADED`, `STALE`).

Reglas principales:

- `READY + OK`: estado normal.
- Un nodo READY que detecta divergencia puede pasar a `DEGRADED` mientras reconcilia; sigue teniendo una base publicada.
- `STALE` sólo tiene sentido mientras **no** está READY.
- Un nodo NOT_READY empieza su reloj de bootstrap; al superar `stale-timeout` pasa a `STALE`.
- La indisponibilidad de un propagador se mide por separado para observabilidad y **no** degrada por sí sola un nodo READY.

El hook `PRE_LOCAL_CONNECT` de readiness tiene prioridad negativa y rechaza **nuevos clientes locales** siempre que `udb_ready == 0`, con un mensaje de indisponibilidad temporal. No expulsa por esa regla a los clientes que ya estaban conectados.

Errores de persistencia que impidan demostrar durabilidad pueden retirar READY aunque el contenido visible ya se haya actualizado; esta política es fail-closed.

## 12. OCL — inventario global de operclasses

OCL es un protocolo distribuido separado de los seis bloques. **No se persiste** en `udb_*.db`.

Cada IRCd participante (servidores no ULine) construye un inventario de las operclasses cargadas. Para cada clase calcula un SHA-256 efectivo que incluye:

- nombre;
- herencia `ISA`;
- ACLs, entradas y variables en su orden efectivo de runtime;
- digest efectivo del padre.

La serialización canónica está limitada a 256 KiB, profundidad ACL 64, profundidad de parent 16 y máximo 1024 operclasses.

Frames OCL:

```text
DB * OCL BEGIN <originSID> <epoch> <generation> <count> <inventoryDigest>
DB * OCL ITEM  <originSID> <epoch> <generation> <name> <effectiveDigest>
DB * OCL END   <originSID> <epoch> <generation>
```

El receptor valida que `originSID` sea un servidor participante visible **alcanzable por el peer directo que entregó el frame**. Sólo tras un END válido y digest correcto hace commit atómico y retransmite el inventario.

Una nueva generación anunciada retira inmediatamente la anterior de la computación global hasta que la nueva complete: comportamiento fail-closed. Un stage OCL expira a los 30 segundos. Se retienen epochs retirados para rechazar frames obsoletos.

En rehash, UDB reconstruye el inventario local; si el digest no cambia, evita incrementar generación y ruido innecesario.

## 13. OCLG — proyección global

OCLG es una vista derivada destinada a suscriptores explícitos, por ejemplo Services. UDB IRCd no necesita suscribirse a OCLG para funcionar.

El registro OCL está `READY` sólo cuando existe inventario local y un snapshot actual para **cada** servidor participante visible. Si falta cualquiera, está `INCOMPLETE`.

Cuando está completo, OCLG contiene la intersección de operclasses que:

1. existen localmente;
2. existen en todos los participantes;
3. tienen exactamente el mismo digest efectivo en todos.

Frames hacia un suscriptor:

```text
DB <subscriber> OCLG BEGIN <epoch> <generation> READY|INCOMPLETE <count> <digest>
DB <subscriber> OCLG ITEM  <epoch> <generation> <name> <digest>
DB <subscriber> OCLG END   <epoch> <generation>
```

La generación OCLG sólo cambia cuando cambia efectivamente la vista o su estado READY/INCOMPLETE.

## 14. Comandos de operador

`DBQ` y `UDB` se registran para usuarios y servidores, pero un usuario local debe ser oper.

### 14.1 `/UDB`

```text
/UDB
/UDB STATUS
/UDB OPERCLASSES [filtro]
/UDB OPERCLASS <nombre>
```

`STATUS` informa mediante numeric 339, entre otros:

- `READY` / `BOOTSTRAPPING`;
- `OK` / `DEGRADED` / `STALE`;
- recuperación `ACTIVE` / `IDLE`;
- propagador seleccionado y fuente directa;
- selector HEL anunciado;
- si sirve downstream;
- origen y texto de la política;
- tiempo sin propagador;
- admisión `ALLOWED` / `DENIED` de nuevos clientes;
- último sync satisfactorio.

`OPERCLASSES` muestra la completitud del registro y los inventarios por servidor. `OPERCLASS <name>` compara la clase contra todos los participantes y sólo puede afirmar globalidad cuando el registro está completo.

### 14.2 `/DBQ`

```text
/DBQ <block>[::path]
/DBQ <server> <block>[::path]
/DBQ STATUS
```

Consultar sólo un bloque devuelve metadatos (registros, tamaño, mtime, checksum y estado de sincronización). Una ruta devuelve su valor o sus hijos inmediatos.

#### Advertencia de redacción de secretos

El código actual de `udb_query_is_secret()` oculta explícitamente:

- `N::*::pass`;
- `N::*::challenge`;
- `S::encryption_key`.

**No incluye `C::*::pass` ni `C::*::challenge` en esa función**, aunque el logger de mutaciones sí trata contraseñas/challenges de canal como sensibles. Por tanto, hasta corregir esa discrepancia, debe considerarse que un oper con acceso a DBQ podría consultar esas credenciales de canal.

## 15. Seguridad e invariantes

La implementación aplica varias reglas de fail-closed:

- no publica candidatos de arranque parcialmente válidos;
- no acepta snapshots de una fuente que no sea la autoridad de esa ronda;
- no aplica un snapshot staged antes de validar checksum y persistencia;
- las mutaciones se validan contra el esquema antes de commit;
- las escrituras de fichero usan temporales exclusivos y permisos `0600`;
- no sigue symlinks en temporales cuando `O_NOFOLLOW` está disponible;
- no borra un temporal si resulta ser symlink o fichero no regular durante la limpieza defensiva;
- un error de durabilidad después de rename retira READY;
- HEL/OCL incompatibles pueden cerrar el enlace, evitando una red mixta silenciosamente inconsistente.

No se recomienda editar `udb_*.db` con el daemon activo. Además de no actualizar el árbol en memoria, una edición manual puede romper generación, esquema, checksum lógico, límites S2S o el conjunto protegido por `.udb_state`.

## 16. Rehash y cambios de política

En rehash:

- la configuración nueva no borra prematuramente el propagador activo si el rehash falla;
- al completar con éxito, si desapareció `udb::propagator`, se elimina el override local y vuelve a aplicarse la precedencia normal;
- se notifica el cambio de política y se cancelan sesiones que ya no pertenezcan a la fuente correcta;
- se reconstruye el inventario OCL local y, si cambia, se publica una nueva generación.

Cambios de `S::propagator` tienen el mismo efecto de re-evaluación en runtime después de que el nuevo bloque/registro haya sido comprometido.

## 17. Build y bundle

Fuentes canónicas: `src/`. Artefacto de distribución: `dist/udb.c`.

```bash
python3 scripts/bundle.py
```

regenera de forma determinista `dist/udb.c` y `modules.list`.

```bash
python3 scripts/bundle.py --check
```

verifica sin escribir que ambos artefactos corresponden exactamente a las fuentes actuales. El generador falla si encuentra una unidad `.c.inc` huérfana o incluida más de una vez.

El flujo CI actual compila UnrealIRCd **6.2.6**, construye el módulo modular desde `src/udb.c`, valida el bundle y ejecuta suites normales y ASan/UBSan, incluyendo bootstrap, staged sync, persistencia, multihop, failover, OCL, límites, spamfilter, canales, clones y validación de esquema.

## 18. Referencia rápida del protocolo DB

```text
HEL 4 <selector> <epoch> OCL [OCLG]
HEL 4 ACK <selector> <epoch> OCL [OCLG]

INF <round> <block> <checksum> <mtime>
RES <round> <block>
BEGIN <round> <block> <txid> <checksum>
PUT <round> <block> <txid> <path> :<string>
PUT <round> <block> <txid> <path> *<number>
END <round> <block> <txid> <checksum>
ACK <round> <block> <txid> <digest>
ERR <subcmd> <code> <round/correlation> <block>

INS <Block::path> <value>
DEL <Block::path>
DRP <block>
OPT <block> [mtime]

OCL BEGIN <originSID> <epoch> <gen> <count> <digest>
OCL ITEM  <originSID> <epoch> <gen> <operclass> <digest>
OCL END   <originSID> <epoch> <gen>

OCLG BEGIN <epoch> <gen> READY|INCOMPLETE <count> <digest>
OCLG ITEM  <epoch> <gen> <operclass> <digest>
OCLG END   <epoch> <gen>
```

## 19. Puntos de observabilidad recomendados

Para diagnosticar un nodo:

1. comprobar `/UDB STATUS`;
2. verificar el propagador seleccionado y que sea un enlace directo con HEL 4 confirmado;
3. revisar si la base está READY o en bootstrap/recovery;
4. usar `/DBQ N`, `/DBQ C`, etc. para comparar checksums y metadatos;
5. usar `/UDB OPERCLASSES` para detectar inventarios OCL ausentes;
6. usar `/UDB OPERCLASS <name>` para detectar diferencias de definición/herencia/ACL;
7. revisar eventos `udb` de UnrealIRCd, especialmente HEL, persistencia, staged sync, READY y OCL;
8. inspeccionar `.udb_state` y las seis generaciones sólo con fines de diagnóstico, sin editarlas en vivo.

## 20. Limitaciones actuales verificadas

A fecha del commit documentado:

- `udb::max-global-clones` está parseado pero no participa en el hook runtime; el límite global efectivo es `S::clones`.
- `S::quit_ips` se carga en contexto pero no tiene consumidor runtime en los fuentes actuales.
- el valor de `C::<canal>::access::<nick>` no define rango; la presencia de la entrada + identificación `+r` es lo que autoriza el JOIN.
- las claves raíz del bloque I se buscan de forma exacta; no son reglas CIDR de matching.
- `DBQ` no redacciona actualmente `C::*::pass`/`challenge`, aunque sí redacciona secretos de N y la clave de cifrado S.
- `PERSISTENT` depende de que exista el modo nativo de canal `+P` en UnrealIRCd; UDB no crea un sustituto.

Estas observaciones describen el comportamiento real del código y son deliberadamente explícitas para evitar documentar capacidades que aún no están conectadas a runtime.
