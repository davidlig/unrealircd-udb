# UDB 4 (Unreal DataBase) - Especificación Técnica y Protocolo

Esta documentación detalla el funcionamiento interno, el diseño de la base de datos y el protocolo Server-to-Server (S2S) del módulo **UDB 4 (Unreal DataBase v4.1.0)** para UnrealIRCd 6.2.x.

Este documento está diseñado para desarrolladores que deseen implementar clientes, servicios (IRC Services) u otros bots compatibles con el protocolo UDB.

## 1. Arquitectura de la Base de Datos

UDB utiliza una estructura de árbol en memoria y almacenamiento en archivos de texto plano para persistencia. La base de datos se divide en "Bloques", identificados por una letra (N, C, I, K, S, L).

### 1.1 Formato de Almacenamiento (Texto Plano) y Codificación de Rutas
El almacenamiento en disco (`udb_X.db`) sigue una estructura jerárquica plana:
```text
; UDB Block N - Version 1
; Saved: 1786942751
; Records: 5
davidlig::pass sha256:abcd1234efgh
davidlig::vhost admin.davidlig.net
davidlig::oper netadmin
```
*   Los subniveles se separan mediante el delimitador `::`.
*   Para garantizar la compatibilidad con direcciones IPv6 (ej. `2001:db8::1`) y caracteres especiales, los componentes de ruta individuales se codifican canónicamente mediante percent-encoding (`%XX`, ej. `2001%3Adb8%3A%3A1`) en disco, en frames S2S y en digests HMAC. En memoria, UDB decodifica transparentemente los componentes para permitir matching nativo de IPs y hosts.
*   Un prefijo `*` en el valor indica que el dato es numérico (entero de 64 bits con validación estricta de formato y límites).
*   La ausencia de `*` indica que el dato es una cadena de texto (String).

### 1.2 Directorio de la Base de Datos, Configuración y Loader Transaccional

#### Directivas en el bloque `udb { }`:
*   `database-directory "<ruta>";`: Selecciona el directorio que contiene los
    archivos de bloque (`udb_N.db`, `udb_C.db`, `udb_I.db`, `udb_S.db`, `udb_L.db`, `udb_K.db`).
    Las rutas locales absolutas se usan tal cual. Las relativas se resuelven bajo `PERMDATADIR`.
    UDB crea el directorio con permisos `0700` de forma segura. Si se omite, se usa `PERMDATADIR`.
*   `propagator "<servidor>";`: Override local estricto del propagador. El valor
    es un único nombre de servidor UnrealIRCd, debe caber en `HOSTLEN` y se
    rechaza durante `CONFIGTEST` si contiene whitespace o CR/LF, o si no supera
    la validación nativa de nombres de servidor. Un valor remoto solo autoriza
    staged sync cuando es un peer directo con `HEL 4` confirmado.
*   `max-staged-records <número>;`: Límite máximo de registros permitidos por bloque en una sesión
    de sincronización transaccional (*staged sync*). Protege contra ataques DoS y agotamiento de
    memoria (OOM). Rango permitido: `1` a `10000000` (por defecto `500000`).
*   `max-global-clones <número>;`: Límite global de conexiones/clones por IP.
*   `password-flood <intentos>:<segundos>;`: Protección de fuerza bruta en contraseñas (por defecto `5:60`).
*   `stale-timeout <segundos>;`: Periodo de gracia antes de transicionar de `DEGRADED` a `STALE` cuando existe una política configurada o presente en el bloque S pero no hay ningún candidato a propagador elegible disponible. Rango permitido: `1` a `604800` segundos (por defecto `300`).
*   `stale-action <warn | deny-new-clients>;`: Política de mitigación operativa aplicada al entrar en estado `STALE`. `deny-new-clients` (por defecto) rechaza limpiamente las nuevas conexiones de clientes locales antes del registro (`HOOKTYPE_PRE_LOCAL_CONNECT`) manteniendo plenamente operativos los clientes existentes y los enlaces S2S; `warn` emite advertencias en el registro sin restringir las conexiones de nuevos clientes.

**Loader Transaccional:** Durante el arranque, UDB lee cada archivo en un árbol
candidato temporal. Solo si el archivo se lee y valida completamente sin errores
se conmuta atómicamente el árbol activo en memoria. Si un archivo no existe
(`ENOENT`), UDB inicia con una base de datos vacía limpia. Si un archivo no se
puede abrir o leer por fallos de permisos (`EACCES`) o errores de E/S de disco,
UDB aborta la inicialización (`MOD_FAILED`) y marca el bloque con estado
`UDB_LOAD_FAILED`, impidiendo terminantemente que el módulo sobreescriba o
destruya los archivos existentes en disco.

### 1.3 Bloques Soportados y sus Opciones

#### Bloque N (Nicks - Usuarios)
Almacena configuraciones para usuarios registrados.
*   **pass**: Hash de contraseña. Solo se aceptan Argon2id (`$argon2id$...`),
     `sha256:` y `crypt:`; texto plano, MD5, bcrypt y valores desconocidos fallan cerrados.
*   **access**: Lista opcional de CIDR IPv4/IPv6 separada por comas o espacios.
    Una comprobación correcta de contraseña para NICK o GHOST también debe
    coincidir con esta lista.
*   **vhost**: Host virtual personalizado a aplicar al conectar.
*   **oper**: Nombre de la clase de IRCop (`operclass`, por ejemplo `locop`, `globop`, `admin`, `services-admin`, `netadmin`, o variantes con `-with-override`). Se valida contra la configuración local mediante `find_operclass()`.
*   **swhois**: Línea extra en el /WHOIS del usuario.
*   **snomasks**: Snomasks a aplicar de forma automática.
*   **modes**: Modos de usuario que se impondrán al identificar.

#### Bloque C (Canales)
*   **founder**: Nick del fundador original del canal (se le otorga +q automáticamente).
*   **modes**: Modos de canal gestionados por UDB. Los parámetros siguen a la cadena de modos. Se validan estrictamente las letras de modo y la cantidad de parámetros requeridos (por ejemplo, `+ntMl` sin parámetro se rechaza, requiriéndose `+ntMl 50`). Al borrarse mediante `DEL`, no se revierten en caliente en el canal.
*   **topic**: El tema (topic) persistente del canal.
*   **access**: Subregistros con los nicks identificados que pueden entrar.
*   **forbid**: Motivo de prohibición del canal.
*   **suspended**: Desactiva el comportamiento de fundador y `+r` del canal registrado.
*   **pass** y **challenge**: Credencial de autenticación de administrador del canal.
*   **options**: Máscara de bits de opciones numéricas (`*<valor>`):
    *   `*1` (`0x1` / `UDB_CHOPT_PROTECT_BANS`): Protege los bans locales (solo el autor puede retirarlos).
    *   `*2` (`0x2` / `UDB_CHOPT_LOCK_MODES`): Bloqueo absoluto de modos de canal (nadie puede modificarlos mediante `MODE` o `SAMODE`).
    *   `*4` (`0x4` / `UDB_CHOPT_LOCK_TOPIC`): Bloqueo absoluto del topic del canal (nadie puede modificarlo mediante `TOPIC`).
    *   `*8` (`0x8` / `UDB_CHOPT_PERSISTENT`): Activa el modo nativo `+P` cuando está cargado el manejador de canales permanentes. Si el canal no existe al insertarse, se crea automáticamente; si queda vacío al desactivarse o borrarse con `DEL`, se destruye.
    *   Admite cualquier combinación de flags (por ejemplo `*6` para `0x2 | 0x4` = lock modes + lock topic, `*14` para `0x2 | 0x4 | 0x8`, etc.).

### 1.3 Reconciliación De Canales En Caliente

UDB controla el estado `+q` del fundador de un canal registrado. Al sustituir
`founder`, retira `+q` al fundador anterior presente y se lo concede al nuevo
fundador identificado. UDB nunca concede `+o` al fundador.

`pass` y `challenge` autentican a un usuario al entrar en ese canal y conceden
únicamente `+a` durante esa membresía. No conceden `+o`. Sustituir o borrar
cualquiera de esas credenciales revoca `+a` solo cuando UDB lo había concedido.
Borrar el perfil del canal revoca los privilegios de fundador y administrador
gestionados por UDB y limpia el topic persistente.

`INVITE <nick> <canal> <contraseña>` valida `C::<#canal>::pass` antes de
ejecutar la invitación nativa. Una invitación local correcta concede al destino
local un permiso de entrada de un solo uso que caduca en cinco minutos. Solo
omite la contraseña de UDB y nunca concede `+a`; una contraseña enviada en
`JOIN` sigue concediendo `+a`. Los INVITE con contraseña para destinos remotos
se rechazan porque el permiso de un solo uso es local y no se transmite por S2S.

Las credenciales UDB solo admiten `challenge` `argon2id`, `sha256` o `crypt`.
Los intentos fallidos se limitan por perfil e IP con `S::flood` o
`udb::password-flood`.

Los efectos de canal de UDB se ejecutan con el cliente ChanServ conectado que
se resuelve desde `S::chanserv`: topics persistentes, modos de canal
configurados y cambios de rangos gestionados por UDB (`q`, `a`, `o`, `h` y
`v`). Se conserva el protocolo nativo de MODE/TOPIC, pero el origen visible es
el cliente de servicio. Si el cliente de servicio no está conectado o la
máscara es ambigua, el evento usa de forma segura el servidor local y UDB
registra la degradación; nunca se fabrica un `Client`.

UDB usa overrides de `MODE`, `SAMODE` y `TOPIC`, no inspección de texto crudo. Con la
opción `*1` (`UDB_CHOPT_PROTECT_BANS`), los `+b` añadidos localmente se asocian a su autor y otro usuario
local no puede eliminarlos, salvo un fundador identificado o un oper.
La opción `*2` (`UDB_CHOPT_LOCK_MODES`) bloquea de forma absoluta cualquier cambio local de modos (incluyendo al fundador).
La opción `*4` (`UDB_CHOPT_LOCK_TOPIC`) bloquea de forma absoluta cualquier cambio local del topic mediante `TOPIC` (incluyendo al fundador).
La opción `*8` (`UDB_CHOPT_PERSISTENT`) mantiene el canal persistente mediante el modo nativo `+P`.

#### Bloque I (IPs y Hosts)
*   **clones**: Límite numérico de conexiones simultáneas (`*<numero>`).
*   **host**: Override explícito para clientes locales coincidentes. UDB guarda
    el host real, cloaked, virtual y sus modos, y los restaura al sustituir o
    borrar el registro, o al descargar el módulo.
*   **nolines**: Letras de excepción de sanciones que se pasan a UnrealIRCd
    (por ejemplo, `GZQSTmc`). UDB solo elimina las excepciones que creó; `c`
    también exime a la IP/host del throttle de clones de UDB.

#### Bloque K (Líneas y Sanciones)
Define las sanciones activas en la red.
*   **G**: G-Line (Bloqueo de usuario@host global).
*   **Z**: Z-Line (Bloqueo de IP global).
*   **S**: Shun (Bloqueo de comunicación global).
*   **Q**: Q-Line (Bloqueo de nicks).
*   **F**: Spamfilter (Bloqueo por expresiones regulares).
    *   *Opciones internas para F:* `type` (target), `action`, `duration`, `reason`.

#### Bloque S (Global / Setup)
Ajustes globales de la red y comportamientos de UDB.
Solo se admiten `clones`, `quit_ips`, `quit_clones`, `flood`, `encryption_key`,
`suffix`, `nickserv`, `chanserv`, `ipserv` y `propagator`.
*   **clones**: Límite numérico global de clones por defecto (`*<numero>`), aplicado
    cuando una IP no tiene límite específico configurado.
*   **quit_clones**: Mensaje de desconexión consumido por el hook de clones.
*   **quit_ips**: Mensaje de desconexión para subsistemas modulares de límite de IP.
*   **flood**: Límite de fallos de contraseña `<intentos>:<segundos>`. Sustituye
    a `udb::password-flood`; al borrarlo se recupera el valor de configuración.
*   **encryption_key** y **suffix**: Una clave HMAC de 64 caracteres hex y un
    sufijo de hostname con puntos, válido e iniciado por `.` habilitan vhosts
    deterministas. UDB calcula HMAC-SHA-256 sobre
    `UDB-vhost-v1|<ip-original>|<host-original>`, convierte los primeros 16
    bytes en 32 hexadecimales minúsculos y añade el sufijo. Ambos registros son
    necesarios; sustituir o borrar uno reconcilia inmediatamente los clientes
    locales conectados. `N::<nick>::vhost` e `I::<ip>::host` explícito tienen
    prioridad sobre el vhost derivado.
*   **nickserv**, **chanserv**, **ipserv**: Máscaras de servicio en formato
    `nick!user@host`. Cada máscara se resuelve dinámicamente contra un único
    usuario ULine conectado, no muerto, mediante el matcher nativo de usuarios
    de UnrealIRCd. El resultado no se guarda en caché. Si no hay una coincidencia
    única, el evento usa el servidor local y UDB registra la degradación segura.
    NickServ es el origen visible de los avisos relacionados con nicks,
    incluidos password incorrecto y bloqueo por flood. IpServ es el origen
    visible de los avisos de vhost explícito de nick, vhost derivado por IP y
    cambios o restauraciones de `I::<ip>::host`.
*   **propagator**: Lista ordenada de prioridad del clúster (por ejemplo,
    `S::propagator "servicios.red.net, hub1.red.net"`). Solo se admiten espacios
    alrededor de las comas y se recortan por token. Se rechazan tokens vacíos,
    tabs, CR/LF, nombres mayores que `HOSTLEN`, nombres inválidos para
    UnrealIRCd y valores totales mayores que `UDB_RECORD_VALUE_MAX`. Una lista
    válida puede superar 512 bytes; ningún token se trunca.

##### Jerarquía de Resolución del Propagador
UDB resuelve la autoridad de forma independiente en cada nodo; una transacción
staged nunca se enruta como protocolo multi-hop:
1. **Override local:** `udb { propagator "<servidor>"; }` tiene precedencia
   estricta. Puede nombrar al servidor local. Un nombre remoto solo es elegible
   si corresponde a un peer de servidor directamente conectado.
2. **Lista persistida:** `S::propagator "pri,sec"` selecciona la primera entrada
   que nombra al servidor local o a un peer de servidor directo. Se omite un
   servidor visible globalmente a través de otro hub.
3. **Autorización HEL:** El peer remoto seleccionado solo puede participar en
   `BEGIN / PUT / END / RES` después de confirmar `HEL 4` en el enlace directo y
   de que la selección anunciada por el peer autorice la transferencia.
4. **Auto-bootstrap:** Un nodo sin override local ni lista persistida válida
   anuncia `HEL 4 ?`, acepta el snapshot inicial de ese peer directo y aprende
   `S::propagator` desde el bloque `S`.

La selección se recalcula cuando cambian los enlaces o los ajustes de
propagator. La disponibilidad ordenada produce failover y failback deterministas
sin conservar punteros `Client *` muertos.

#### Bloque L (Enlaces S2S)
Solo se admite `L::<servidor>::options`. Su máscara numérica usa `*1` para
notices de depuración UDB.

---

## 2. Protocolo S2S (Server-to-Server)

El protocolo UDB se integra en el tráfico S2S nativo de UnrealIRCd utilizando el comando extendido `DB`.
Los peers directamente enlazados que completan explícitamente el intercambio
HEL de UDB tienen capacidad de protocolo UDB V4. La capacidad no autoriza el
acceso a datos: las importaciones staged `BEGIN`, `PUT` y `END`, y las
peticiones y exportaciones `RES`, se aceptan únicamente en la dirección estricta
de autoridad: un nodo fresco sin ninguna política de propagador solo puede ser
alimentado por su único peer de bootstrap exclusivo, y un nodo con política
acepta datos staged solo del peer directo que seleccionó como propagador. Una
vez READY sin política, el nodo es su propia autoridad standalone y no acepta
importaciones remotas. Esto permite propagación A-a-B-a-C local por
enlace cuando B selecciona A y C selecciona B, así como arquitecturas de Ingest Gateway donde un Hub aísla a los Servicios del resto de la red. Las mutaciones en caliente deben
proceder igualmente de la autoridad configurada; un peer que sirve una
sincronización autorizada solo puede enviar registros de ese bloque.

Para garantizar la integridad y coherencia de los datos en toda la red, se recomienda
configurar el bloque `require module { name "third/udb"; };` en `unrealircd.conf`. Esto
hace que el servidor aborte de forma inmediata (`SQUIT`) cualquier intento de enlace
con un nodo que no tenga UDB activo durante el handshake inicial `SMOD`.

**Estructura general:**
`:<sid_origen> DB <destino> <subcomando> <parametros>`

HEL 4 exige un ID de ronda decimal distinto de cero en cada frame de inventario,
transferencia staged y error. Los frames que no cumplen esta gramática se
rechazan.

### 2.1 Sincronización Inicial (Handshake)
Cuando un servidor se conecta a otro, se verifica el estado de los bloques con
un CRC32 de los registros lógicos canónicos. El digest ordena los registros
serializados `ruta valor`, por lo que los encabezados, timestamps de guardado y
el orden de inserción no lo afectan. Tras `HOOKTYPE_SERVER_SYNC`, cada peer
directamente enlazado recibe una petición `HEL 4 <propagador-seleccionado>`. Solo el `HEL 4 ACK` directo
correspondiente confirma UDB V4 para ese enlace; antes no se envían `INF`, frames
staged ni frames DB UDB reenviados. Si no llega el acuse en 60 segundos, el enlace
se aborta automáticamente mediante `SQUIT` para proteger la red de desincronizaciones.
`HEL` es el único frame DB
aceptado antes de confirmar y nunca se reenvía fuera del enlace directo.

**HEL (Negociación de capacidad y Auto-Bootstrap):**
`:<sid> DB <sid-peer-directo> HEL 4 <propagador-seleccionado>`

El campo de propagador seleccionado es `?` solo cuando no existe ninguna fuente
de propagator configurada y el nodo aún no está READY. Permite a ese nodo
descubrir la autoridad del clúster y
autoriza al peer de bootstrap exclusivo a entregar el snapshot inicial. Un nodo
READY sin política es una autoridad standalone y se anuncia con su propio nombre
en lugar de `?`. Un servidor configurado
pero no disponible se anuncia como `HEL 4 -`, no como `?`; `-` no concede
autorización staged y evita ampliar el acceso de forma silenciosa.

**Acuse HEL:**
`:<sid> DB <sid-peer-directo> HEL 4 ACK`

**INF (Información del Bloque):**
`:<sid> DB <destino> INF <id_ronda> <letra_bloque> <crc32_hex> <timestamp>`

**RES (Request / Petición de Sincronización):**
`:<sid> DB <destino> RES <id_ronda> <letra_bloque>`

El timestamp de `INF` es metadata informativa (logs, diagnóstico y
`/UDB STATUS`); nunca decide la autoridad. Cuando los checksums difieren, el
receptor siempre solicita el bloque a su autoridad seleccionada mediante `RES`:
la autoridad es el propagador directo configurado (o el peer de bootstrap
exclusivo antes de READY), nunca una comparación de timestamps o SIDs. Solo el
follower envía `RES`; una autoridad nunca solicita a sus followers, lo que
evita ciclos recíprocos de RES y snapshots.

Para peers con capacidad staged, `RES` se responde mediante una transacción:

**BEGIN:** `:<sid> DB <destino> BEGIN <id_ronda> <bloque> <txid> <digest>`

**PUT:** `:<sid> DB <destino> PUT <id_ronda> <bloque> <txid> <ruta> :<valor>`

**END:** `:<sid> DB <destino> END <id_ronda> <bloque> <txid> <digest>`

**ACK:** `:<sid> DB <destino> ACK <id_ronda> <bloque> <txid> <digest>`

El receptor solo acepta `BEGIN` si previamente emitió `RES` para el mismo peer
directo, bloque y ronda activa. Los frames `INF`, `BEGIN`, `PUT` o `END` tardíos
de otra ronda no pueden avanzar la ronda actual. El solicitante y el receptor de
esta transacción deben corresponder al propagador directo
seleccionado. Un peer confirmado por HEL pero no seleccionado recibe
`UDB_ERR_FORBIDDEN` para `RES`, `BEGIN`, `PUT` y `END`; no puede crear ni
continuar una sesión staged, provocar una exportación de bloque ni hacer que
esos frames se reenvíen.

Las rutas de `PUT` omiten el prefijo del bloque porque el bloque es un parámetro
explícito. Los registros persistidos deben tener componentes de ruta `::` no
vacíos y caber dentro de los límites `UDB_RECORD_LINE_MAX` (12320 bytes) y
`UDB_RECORD_PATH_MAX` (8192 bytes). Si se encuentra cualquier línea malformada,
demasiado larga o que no cumpla con el esquema durante la carga del archivo `.db`,
UDB aborta la carga de forma transaccional fail-closed con `UDB_LOAD_FAILED`,
descarta el candidato y registra un error fatal, evitando corrupciones o
arranques en estado parcial. El receptor construye un árbol aislado por bloque y
no aplica efectos en tiempo real durante la transferencia. En `END` valida el digest
canónico, persiste el árbol staged atómicamente en el archivo temporal y solo entonces
reemplaza el árbol activo. Una desconexión del peer, el vencimiento del timeout de
inactividad configurable (por defecto 60s), el timeout absoluto configurable
(por defecto 300s), superar los límites de registros/bytes staged, un `PUT` inválido,
un txid inesperado o un digest incorrecto descarta solo el árbol staged; el árbol
activo y durable anterior no cambian.
Un digest de `END` solo es válido si todo su campo no vacío es hexadecimal y
cabe en `unsigned long`; se rechazan entradas parciales y desbordamientos,
incluso cuando un árbol staged vacío tiene digest cero.

Mientras exista una transacción staged, `INS`, `DEL`, `DRP` y `OPT` se rechazan
con `UDB_ERR_SYNC_ACTIVE`, incluso desde el propagador. Fuera de una transacción
esas mutaciones siguen requiriendo el propagador configurado.

### 2.2 Modificación de Datos en Tiempo Real
Para inyectar o eliminar registros en caliente, se usan los siguientes comandos (generalmente con destino `*` para broadcast).

**INS (Insertar / Modificar):**
`:<sid> DB * INS <letra_bloque>::<clave>[::<subclave>] <valor>`
*Ejemplo:* `<sid> DB * INS N::davidlig::vhost admin.davidlig.net`

**DEL (Eliminar):**
Elimina un nodo en cascada.
`:<sid> DB * DEL <letra_bloque>::<clave>[::<subclave>]`
*Ejemplo:* `:<sid> DB * DEL C::#opers::topic`

Tras confirmar HEL 4, `INS` y `DEL` en tiempo real solo se aceptan del
propagador seleccionado, salvo frames del peer que está sirviendo la
sincronización de ese bloque. Se rechazan mientras el bloque tenga una
transacción staged y solo se persisten y reenvían a peers directos con HEL
confirmado.

UDB valida estrictamente mediante un sistema declarativo de esquemas y límites numéricos que
cualquier registro recibido vía `INS`, `PUT` o cargado desde disco pertenezca al
catálogo de opciones válidas del bloque correspondiente y cumpla con su tipo de
dato y formato. Claves desconocidas, anidamientos no permitidos (como rutas
compuestas en Bloque S), líneas sobrelongitud o tipos incompatibles son rechazados inmediatamente con
`ERR INS 2 <id_correlacion> <bloque>` o `ERR PUT 2 <id_ronda> <bloque>` (`UDB_ERR_PARAMS`), y provocan que la
carga de archivos `.db` aborte de manera estricta y transaccional (**fail-closed**), descartando cualquier
cambio candidato y preservando intacta la base de datos previa.

### 2.2 Jerarquía de Límites Numéricos e Invariantes Matemáticos

Para garantizar que ningún registro sufra truncado en ninguna etapa (memoria, serialización en disco o propagación S2S), UDB define y aplica una jerarquía unificada de límites:

| Parámetro | Límite (bytes) | Constante | Descripción / Fórmula Invariante |
|---|---|---|---|
| Longitud máxima de ruta | 8.192 | `UDB_RECORD_PATH_MAX` | Longitud total de la ruta (`bloque::k1::...::kN`) |
| Componente raw máximo | 4.608 | `UDB_COMPONENT_RAW_MAX` | Componente decodificado (`4096 + 4` para prefijo `b64:`) |
| Componente codificado máx. | 4.608 | `UDB_COMPONENT_ENCODED_MAX` | Componente percent-encoded (`4102` para `b64%3A...`) |
| Longitud máxima de valor | 4.096 | `UDB_RECORD_VALUE_MAX` | Carga útil de datos (topic, vhost, razón, clave, etc.) |
| Línea máxima en disco | 12.320 | `UDB_RECORD_LINE_MAX` | `PATH_MAX (8192) + VALUE_MAX (4096) + 32` bytes de margen |
| Marco S2S máximo | 16.384 | `UDB_S2S_LINE_MAX` | `MAXLINELENGTH` (límite de tramas S2S `BIGLINES` de UnrealIRCd 6) |
| Margen de protocolo S2S | 256 | `UDB_S2S_OVERHEAD_MAX` | Espacio para cabecera `:SID DB SID CMD ...` |
| Regex de spamfilter máx. | 3.072 | `UDB_SPAMFILTER_PATTERN_MAX` | Longitud máxima de regex sin codificar |

#### Validación Matemática de Codificación Spamfilter:
- Expresión regular raw: $\le 3072$ bytes (`UDB_SPAMFILTER_PATTERN_MAX`).
- Codificación Base64 RFC 4648: $\lceil 3072 / 3 \rceil \times 4 = 4096$ caracteres.
- Componente raw con prefijo `b64:`: $4 + 4096 = 4100$ bytes ($\le \text{UDB\_COMPONENT\_RAW\_MAX } 4608$).
- Componente percent-encoded (`b64%3A`): $4100 + 2 = 4102$ bytes ($\le \text{UDB\_COMPONENT\_ENCODED\_MAX } 4608$).
- Ruta completa (`K::F::b64%3A...::reason`): $4116$ bytes ($\le \text{UDB\_RECORD\_PATH\_MAX } 8192$).
- Línea serializada en disco: $4116 + 1 + 4096 + 1 = 8214$ bytes ($\le \text{UDB\_RECORD\_LINE\_MAX } 12320$).
- Trama de red S2S: $8214 + 256 = 8470$ bytes ($\le \text{UDB\_S2S\_LINE\_MAX } 16384$).

Para `INS`, `DEL` y `DRP`, UDB primero clona el bloque activo y aplica el
cambio al candidato privado. Solo después de escribir y renombrar atómicamente
su snapshot actualiza índices, contadores y efectos en tiempo real. Al reemplazar
un registro mediante `INS`, primero revoca sus efectos anteriores y después
aplica el candidato, incluida una degradación de `N::<nick>::oper`. Un `INS`
cuyo valor es idéntico al ya almacenado es idempotente: se persiste sin revocar
ni re-aplicar efectos, de modo que reenviar `C::<#canal>::modes` con el mismo
valor no genera cambios de modos ni revoca el `+q` del fundador. Al sustituir el
perfil de un canal (`C::<#canal>`), la revocación previa se restaura desde el
perfil sobreviviente: fundador, modos, `+P` y topic. `OPT` también
escribe el snapshot antes de actualizar metadatos o reenviar. Si falla la
persistencia, el estado y archivo activos no cambian, se devuelve `ERR` y la
mutación no se reenvía.

Los snapshots se crean de forma exclusiva con modo `0600`, independiente de la
umask del proceso. Cuando la plataforma proporciona `O_NOFOLLOW`, se usa como
protección adicional contra enlaces simbólicos. UDB vacía y ejecuta `fsync` del
snapshot temporal antes de cerrarlo y renombrarlo, y después ejecuta `fsync` del
directorio contenedor. UDB aborta y elimina su snapshot temporal ante un fallo
de apertura, permisos, flujo, sincronización del archivo, cierre o renombrado.
El `rename` exitoso es el punto irreversible: si después falla el `fsync` del
directorio, UDB conserva el snapshot visible como estado activo para evitar una
divergencia entre memoria y disco, marca `.udb_state` como `BOOTSTRAPPING`,
devuelve un error de persistencia y no confirma la ronda ni permite `READY`.
Si el mismo fallo ocurre después del `rename` visible de `.udb_state` a `READY`,
UDB mantiene `udb_ready=0` y sustituye el marcador visible por
`BOOTSTRAPPING`; el resultado se clasifica como commit con durabilidad incierta,
no como un fallo anterior al commit.

**DRP (Drop / Vaciar Bloque):**
`:<sid> DB * DRP <letra_bloque>`

**OPT (Optimizar):**
`:<sid> DB * OPT <letra_bloque>`

### 2.3 Manejo de Errores (ERR)
Todos los errores usan exclusivamente:
`:<sid> DB <destino> ERR <subcomando> <codigo_error> <id_ronda> <bloque>`

`id_ronda` debe ser decimal, estricto y distinto de cero. Un `ERR` solo puede
limpiar una solicitud pendiente, abortar una sesión staged o abortar la ronda si
el peer directo, el bloque y el ID coinciden con el estado vigente. Los errores
tardíos se ignoran. Para errores de mutaciones en tiempo real (`INS`, `DEL`,
`DRP` y `OPT`), el campo contiene un identificador de correlación local no nulo;
esos comandos nunca pueden modificar el estado de reconciliación.
*   `1`: UDB_ERR_NO_BLOCK (El bloque especificado no existe)
*   `2`: UDB_ERR_PARAMS (Parámetros insuficientes o inválidos)
*   `3`: UDB_ERR_FATAL (Error interno fatal o fallo de persistencia)
*   `4`: UDB_ERR_SYNC_ACTIVE (Ya hay una sincronización en curso)
*   `5`: UDB_ERR_NO_SYNC (No se ha solicitado una sincronización)
*   `6`: UDB_ERR_FORBIDDEN (Acción denegada por permisos / no es propagador)

### 2.4 Readiness Operativa, Persistencia y Salud (Invariantes R1 - R9)

UDB aplica invariantes deterministas para readiness de la base de datos,
persistencia durable, reconciliación aislada por rondas y topología salto a salto:

1. **Readiness Durable (Invariante R1):** `udb_ready=1` solo se alcanza después de persistir de forma durable los seis bloques (`N, C, I, S, L, K`), escribir atómicamente `.udb_state` con `STATE=READY` —incluido `fsync` del directorio— y, únicamente entonces, establecer `udb_ready=1`.
2. **Validación de Snapshots al Reiniciar (Invariante R2):** si `.udb_state` indica `READY` pero falta cualquier snapshot `udb_X.db` requerido, o su generación no coincide, el nodo no inicia en `READY`; falla de forma cerrada a `BOOTSTRAPPING`.
3. **Integridad de Bootstrap (Invariante R3):** un nodo en bootstrap no puede servir snapshots staged downstream (`RES` se rechaza con `ERR RES FORBIDDEN`) ni aceptar clientes locales normales.
4. **Independencia entre Readiness y Salud (Invariante R4):** `READY + OK`, `READY + DEGRADED` y `READY + STALE` son estados válidos e independientes. Perder el upstream no elimina un `udb_ready=1` durable.
5. **Autoridad Directa y S2S Salto a Salto (Invariante R5):** la sincronización staged es estrictamente salto a salto. En `Services A -> Hub B -> Leaf C`, B selecciona A como autoridad directa y C selecciona B. Las transacciones `BEGIN`/`PUT`/`END`/`RES` nunca se enrutan de forma transparente entre varios saltos.
6. **Aislamiento y Ciclo de Vida de Rondas (Invariante R6):** cada ronda tiene un `round_id` explícito y máscaras aisladas (`compared_blocks`, `divergent_blocks`, `completed_blocks`). Las máscaras se reinician al iniciar una ronda nueva, incluso con el mismo peer, y `END` verifica que la sesión pertenezca a la ronda activa.
7. **Convergencia de Reconciliación (Invariante R7):** la transición a `READY` exige comparar los seis bloques en la ronda activa, confirmar todos los bloques divergentes en esa ronda y no dejar sesiones activas ni solicitudes pendientes.
8. **Estado RES Pendiente Acotado (Invariante R8):** `pending_from`, `pending_deadline` y `pending_round_id` se controlan por separado de la sesión activa. Solo un `ERR` de la misma ronda, además de `BEGIN`, timeout, desconexión, cambio de política y shutdown, puede limpiar ese estado. Los fallos vigentes abortan toda la ronda y programan un retry con backoff exponencial y límite finito.
9. **Snapshots Huérfanos Fallan Cerrados:** cuando falta `.udb_state` no se intenta ninguna migración de almacenamiento. Incluso una base completa y cargable de seis snapshots permanece en `BOOTSTRAPPING` (clientes denegados), registra `UDB_ORPHANED_SNAPSHOTS` y exige un bootstrap autorizado o una acción explícita del operador. Los formatos de estado obsoletos (archivos mínimos `STATE=`/`LAST_SYNC=`, `ORIGIN=LEGACY`) son inválidos y fallan cerrados.
10. **Consistencia ante Fallos:** `.udb_state.tmp` se renombra atómicamente a `.udb_state` y se ejecuta `fsync` sobre el descriptor del directorio contenedor antes de cerrarlo. Si falla el `fsync` posterior a un rename visible, el estado operativo permanece `BOOTSTRAPPING`; nunca se promueve `READY` con durabilidad incierta.

### 2.5 Máquina de Estados de Salud Operativa (OK / DEGRADED / STALE)
UDB implementa una máquina de estados determinista para gestionar la fiabilidad operativa del nodo:
*   **`OK`**: Hay un candidato a propagador upstream directamente conectado y confirmado con `HEL 4` (o el propio nodo local es el propagador autorizado, o no hay política definida). La sincronización completa de base de datos y la operativa de clientes se desarrollan con normalidad.
*   **`DEGRADED`**: Existe una política de propagador (vía configuración local o bloque `S`), pero actualmente no hay ningún candidato elegible utilizable. El nodo opera dentro del periodo de gracia configurable (`stale-timeout`, por defecto 300s). Las mutaciones salto a salto (`INS`/`DEL`/`DRP`/`OPT`) continúan propagándose con normalidad, los clientes existentes permanecen conectados y se admiten nuevos clientes locales.
*   **`STALE`**: El periodo de gracia ha expirado sin encontrar un propagador elegible. Si `stale-action deny-new-clients` está activo (por defecto), se rechazan limpiamente las nuevas conexiones de clientes locales con un aviso fatal antes del registro (`HOOKTYPE_PRE_LOCAL_CONNECT`). Los clientes existentes y los enlaces S2S nunca se desconectan.

**Invariantes Fundamentales:**
1.  **Invariante Estricta de Confianza:** El tiempo transcurrido *nunca* convierte el estado anunciado `HEL 4 -` en `HEL 4 ?` ni relaja las reglas de confianza. Un nodo aislado con política obsoleta jamás aceptará automáticamente snapshots por etapas de vecinos no autorizados.
2.  **Recuperación Automática:** En cuanto un propagador elegible se conecta y confirma `HEL 4`, el nodo transiciona de inmediato a `OK`, emite el evento `UDB_SYNC_RECOVERED` y reanuda la admisión de nuevos clientes sin requerir reinicio del IRCd.
3.  **Override Administrativo:** Los administradores pueden recuperar un nodo en estado stale actualizando `propagator "<nuevo-servidor>";` en `unrealircd.conf` y ejecutando `/REHASH`. El override local tiene precedencia sobre políticas obsoletas en base de datos.

### 2.6 Comandos de Diagnóstico y Estado para Operadores
Los operadores pueden consultar en tiempo real el estado de sincronización mediante `/UDB STATUS` o `/DBQ STATUS` (solo opers):
```text
/UDB STATUS
```
Salida:
```text
:server 339 oper :UDB synchronization: OK | DEGRADED | STALE
:server 339 oper :Database readiness: READY | BOOTSTRAPPING
:server 339 oper :Selected propagator: <server> | none
:server 339 oper :Advertised state: HEL 4 <server|?|->
:server 339 oper :Policy source: local | S | none
:server 339 oper :Policy: <list>
:server 339 oper :Time without propagator: <seconds>
:server 339 oper :New local clients: ALLOWED | DENIED
:server 339 oper :Last successful synchronization: <timestamp> | none
```

### 2.7 Redacción de Secretos en DBQ

`DBQ` requiere privilegios de oper y nunca devuelve el valor de `pass`,
`challenge` ni `encryption_key`. Las consultas directas y los listados de hijos
muestran `<redacted>` para esos registros.

---

## 3. Créditos y Licencia

**Autor y Desarrollador Principal:**
El módulo UDB, su arquitectura modular moderna para UnrealIRCd 6 y las extensiones del protocolo v4 son desarrollados y mantenidos por **David Abuín Fontán ('davidlig')** (<https://github.com/davidlig/unrealircd-udb>).

**Concepto e Idea Original:**
Basado en el concepto original del protocolo UDB concebido por **Trocotronic** (*www.redyc.com*).
