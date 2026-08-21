# UDB (Unreal DataBase) v4 - Especificación Técnica y Protocolo

Esta documentación detalla el funcionamiento interno, el diseño de la base de datos y el protocolo Server-to-Server (S2S) del módulo **UDB (Unreal DataBase) v4.0.0** para UnrealIRCd 6.2.x.

Este documento está diseñado para desarrolladores que deseen implementar clientes, servicios (IRC Services) u otros bots compatibles con el protocolo UDB.

## 1. Arquitectura de la Base de Datos

UDB utiliza una estructura de árbol en memoria y almacenamiento en archivos de texto plano para persistencia. La base de datos se divide en "Bloques", identificados por una letra (N, C, I, K, S, L).

### 1.1 Formato de Almacenamiento (Texto Plano)
El almacenamiento en disco (`udb_X.db`) sigue una estructura jerárquica plana:
```text
; UDB Block N - Version 1
; Saved: 1786942751
; Records: 5
davidlig::pass sha256:abcd1234efgh
davidlig::vhost admin.davidlig.net
davidlig::oper *4
```
*   Los subniveles se separan mediante el delimitador `::`.
*   Un prefijo `*` en el valor indica que el dato es numérico (entero).
*   La ausencia de `*` indica que el dato es una cadena de texto (String).

### 1.2 Directorio de la Base de Datos
`udb::database-directory` selecciona el directorio que contiene todos los
archivos de bloque: `udb_N.db`, `udb_C.db`, `udb_I.db`, `udb_S.db`, `udb_L.db` y
`udb_K.db`. Las rutas locales absolutas se usan tal cual. Las rutas relativas se
resuelven bajo `PERMDATADIR` de UnrealIRCd. La opción rechaza URLs, saltos de
línea y rutas que no pueden contener de forma segura un nombre de bloque. UDB
crea el directorio final con modo `0700` cuando es necesario y rechaza iniciar
si no es un directorio o no puede crearlo. Si se omite, se conserva la ubicación
heredada: el propio `PERMDATADIR`.

### 1.3 Bloques Soportados y sus Opciones

#### Bloque N (Nicks - Usuarios)
Almacena configuraciones para usuarios registrados.
*   **pass**: Hash de contraseña. Solo se aceptan Argon2id (`$argon2id$...`),
     `sha256:` y `crypt:`; texto plano, MD5, bcrypt y valores desconocidos fallan cerrados.
*   **access**: Lista opcional de CIDR IPv4/IPv6 separada por comas o espacios.
    Una comprobación correcta de contraseña para NICK o GHOST también debe
    coincidir con esta lista.
*   **vhost**: Host virtual personalizado a aplicar al conectar.
*   **oper**: Nivel de IRCop (`*1` = Helper, `*2` = Admin, `*4` = Root).
*   **swhois**: Línea extra en el /WHOIS del usuario.
*   **snomasks**: Snomasks a aplicar de forma automática.
*   **modes**: Modos de usuario que se impondrán al identificar.

#### Bloque C (Canales)
*   **founder**: Nick del fundador original del canal (se le otorga +q automáticamente).
*   **modes**: Modos de canal gestionados por UDB. Los parámetros siguen a la cadena de modos.
*   **topic**: El tema (topic) persistente del canal.
*   **access**: Subregistros con los nicks identificados que pueden entrar.
*   **forbid**: Motivo de prohibición del canal.
*   **suspended**: Desactiva el comportamiento de fundador y `+r` del canal registrado.
*   **pass** y **challenge**: Credencial de autenticación de administrador del canal.
*   **persistent**: Activa el `+P` nativo cuando está cargado el manejador de canales permanentes.
*   **options**: Opciones numéricas: `*1` protege los bans locales y `*2`
     bloquea cualquier cambio local de `MODE` o `SAMODE` salvo para el fundador
     identificado.

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

UDB usa overrides de `MODE` y `SAMODE`, no inspección de texto crudo. Con la
opción `*1`, los `+b` añadidos localmente se asocian a su autor y otro usuario
local no puede eliminarlos, salvo un fundador identificado o un oper.
La opción `*2` rechaza todo cambio local de modos de quien no sea el fundador;
no se limita a los modos guardados en `C::<#canal>::modes`.

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
Solo se admiten `quit_ips`, `quit_clones`, `flood`, `encryption_key`, `suffix`,
`nickserv`, `chanserv` e `ipserv`.
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
    `nick!user@host`.
*   **quit_ips** y **quit_clones**: Estado validado para mensajes de expulsión;
    el hook de clones consume `quit_clones`.

#### Bloque L (Enlaces S2S)
Solo se admite `L::<servidor>::options`. Su máscara numérica usa `*1` para
notices de depuración UDB y `*2` para seleccionar el propagador. Debe existir
exactamente una fuente: `udb::propagator` o un registro `L` con `*2`; cero o
más de una rechazan escrituras remotas. `prefix` y `allow_clients` no son
ajustes UDB soportados.

---

## 2. Protocolo S2S (Server-to-Server)

El protocolo UDB se integra en el tráfico S2S nativo de UnrealIRCd utilizando el comando extendido `DB`.
Los peers directamente enlazados que completan explícitamente el intercambio
HEL de UDB tienen capacidad de protocolo UDB V4. La capacidad no autoriza el
acceso a datos: las importaciones staged `BEGIN`, `PUT` y `END`, y las
peticiones y exportaciones `RES`, solo se aceptan del peer directo seleccionado
como propagador configurado. Esto permite propagación A-a-B-a-C local por
enlace cuando B selecciona A y C selecciona B. Las mutaciones en caliente deben
proceder igualmente del `udb::propagator` configurado; un peer que sirve una
sincronización autorizada solo puede enviar registros de ese bloque.
**Estructura general:**
`:<sid_origen> DB <destino> <subcomando> <parametros>`

### 2.1 Sincronización Inicial (Handshake)
Cuando un servidor se conecta a otro, se verifica el estado de los bloques con
un CRC32 de los registros lógicos canónicos. El digest ordena los registros
serializados `ruta valor`, por lo que los encabezados, timestamps de guardado y
el orden de inserción no lo afectan. Tras `HOOKTYPE_SERVER_SYNC`, cada peer
directamente enlazado recibe una petición `HEL 4 <propagador-seleccionado>`. Solo el `HEL 4 ACK` directo
correspondiente confirma UDB V4 para ese enlace; antes no se envían `INF`, frames
staged ni frames DB UDB reenviados. Si no llega el acuse en 60 segundos, el enlace
queda marcado como no compatible hasta reconectar. `HEL` es el único frame DB
aceptado antes de confirmar y nunca se reenvía fuera del enlace directo.

**HEL (Negociación de capacidad):**
`:<sid> DB <sid-peer-directo> HEL 4 <propagador-seleccionado>`

El campo de propagador seleccionado es `-` cuando no existe una fuente única
configurada. Permite al peer directo autorizar snapshots salientes solo cuando
el receptor lo ha seleccionado explícitamente.

**Acuse HEL:**
`:<sid> DB <sid-peer-directo> HEL 4 ACK`

**INF (Información del Bloque):**
`:<sid> DB <destino> INF <letra_bloque> <crc32_hex> <timestamp>`

**RES (Request / Petición de Sincronización):**
`:<sid> DB <destino> RES <letra_bloque>`

Cuando los checksums difieren, gana el `timestamp` más reciente. Si los
timestamps son iguales, gana el SID de servidor lexicográficamente mayor. El
SID es la identidad inmutable del servidor que ya contiene el frame DB, a
diferencia de un nombre configurable o del orden de llegada del enlace. Solo el
perdedor envía `RES` por bloque; esto evita intercambios recíprocos de RES y snapshots sin
alterar las comprobaciones del propagador directo configurado.

Para peers con capacidad staged, `RES` se responde mediante una transacción:

**BEGIN:** `:<sid> DB <destino> BEGIN <bloque> <txid> <digest>`

**PUT:** `:<sid> DB <destino> PUT <bloque> <txid> <ruta> :<valor>`

**END:** `:<sid> DB <destino> END <bloque> <txid> <digest>`

**ACK:** `:<sid> DB <destino> ACK <bloque> <txid> <digest>`

El solicitante y receptor de esta transacción debe ser el propagador directo
seleccionado. Un peer confirmado por HEL pero no seleccionado recibe
`UDB_ERR_FORBIDDEN` para `RES`, `BEGIN`, `PUT` y `END`; no puede crear ni
continuar una sesión staged, provocar una exportación de bloque ni hacer que
esos frames se reenvíen.

Las rutas de `PUT` omiten el prefijo del bloque porque el bloque es un parámetro
explícito. El receptor construye un árbol aislado por bloque y no aplica efectos
en tiempo real durante la transferencia. En `END` valida el digest canónico,
persiste el árbol staged atómicamente en el archivo temporal y solo entonces
reemplaza el árbol activo. Una desconexión del peer, 60 segundos sin actividad,
un `PUT` inválido, un txid inesperado o un digest incorrecto descarta solo el
árbol staged; el árbol activo y durable anterior no cambian.

Mientras exista una transacción staged, `INS`, `DEL`, `DRP` y `OPT` se rechazan
con `UDB_ERR_SYNC_ACTIVE`, incluso desde el propagador. Fuera de una transacción
esas mutaciones siguen requiriendo el propagador configurado.

`FDR` no se emite por el protocolo HEL 4 y no forma parte de la transferencia
staged.

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
Para `INS`, `DEL` y `DRP`, UDB primero clona el bloque activo y aplica el
cambio al candidato privado. Solo después de escribir y renombrar atómicamente
su snapshot actualiza índices, contadores y efectos en tiempo real. Al reemplazar
un registro mediante `INS`, primero revoca sus efectos anteriores y después
aplica el candidato, incluida una degradación de `N::<nick>::oper`. `OPT` también
escribe el snapshot antes de actualizar metadatos o reenviar. Si falla la
persistencia, el estado y archivo activos no cambian, se devuelve `ERR` y la
mutación no se reenvía.

Los snapshots se crean de forma exclusiva con modo `0600`, independiente de la
umask del proceso. Cuando la plataforma proporciona `O_NOFOLLOW`, se usa como
protección adicional contra enlaces simbólicos. UDB aborta y elimina su snapshot
temporal ante un fallo de apertura, permisos, flujo, cierre o renombrado; los
snapshots y bloques activos siempre permanecen bajo el directorio de base de
datos configurado.

**DRP (Drop / Vaciar Bloque):**
`:<sid> DB * DRP <letra_bloque>`

**OPT (Optimizar):**
`:<sid> DB * OPT <letra_bloque>`

### 2.3 Manejo de Errores (ERR)
`:<sid> DB <destino> ERR <subcomando> <codigo_error> <extra>`
*   `1`: UDB_ERR_NO_BLOCK (El bloque especificado no existe)
*   `7`: UDB_ERR_SYNC_ACTIVE (Ya hay una sincronización en curso)
*   `8`: UDB_ERR_NO_SYNC (No se ha solicitado una sincronización)
*   `9`: UDB_ERR_FORBIDDEN (Acción denegada por permisos)

### 2.4 Redacción De Secretos En DBQ

`DBQ` requiere privilegios de oper y nunca devuelve el valor de `pass`,
`challenge` ni `encryption_key`. Las consultas directas y los listados de hijos
muestran `<redacted>` para esos registros.

---

## 3. Créditos y Licencia

**Autor Original (UnrealIRCd 3.x):**
El protocolo UDB y su versión original clásica fueron ideados y desarrollados por **Trocotronic** (*www.redyc.com*). El módulo actual y las optimizaciones del protocolo son desarrolladas y mantenidas bajo la URL del proyecto **https://github.com/davidlig/unrealircd-udb** por **David Abuín Fontán ('davidlig')**.

**Versión Actual (UnrealIRCd 6.2.x - UDB v4.0.0):**
Refactorización moderna, paso a la nueva arquitectura C modular y adaptación integral a la nueva API v6 de UnrealIRCd desarrollados por **David Abuín Fontán "davidlig"** (2026).
Se ha logrado convertir UDB en un módulo de terceros estándar y optimizado (`udb.so`), con integración nativa al motor de TKLs, seguridad criptográfica moderna y soporte total para el enrutamiento S2S v6.
