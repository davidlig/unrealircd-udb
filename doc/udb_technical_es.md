# UDB 4 (Unreal DataBase) - Especificación Técnica y Protocolo

Esta documentación detalla el funcionamiento interno, el diseño de la base de datos y el protocolo Server-to-Server (S2S) del módulo **UDB 4 (Unreal DataBase v4.0.0)** para UnrealIRCd 6.2.x.

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
davidlig::oper netadmin
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
*   **propagator**: Autoridad o lista ordenada de servidores autorizados para emitir mutaciones y snapshots en el clúster (ej: `S::propagator "servicios.red.net,hub1.red.net"`).

##### Jerarquía de Resolución del Propagador
UDB evalúa la autoridad activa mediante un modelo jerárquico determinista y tolerante a fallos:
1. **Prioridad 1 (Override Local):** Si `udb { propagator "servidor"; }` está definido en `unrealircd.conf`, tiene precedencia absoluta sobre la red.
2. **Prioridad 2 (Lista de Prioridad en BD):** Si existe `S::propagator "pri,sec"`, se selecciona el primer servidor de la lista que se encuentre actualmente online (`FindServer`), permitiendo Failover y Failback automático sin intervención manual.
3. **Modo Bootstrap / Clean Node:** Si no hay propagador configurado localmente, el nodo acepta la sincronización inicial del peer directo enlazado vía `HEL 4 ?` y aprende la autoridad dinámicamente desde el bloque `S`.

#### Bloque L (Enlaces S2S)
Solo se admite `L::<servidor>::options`. Su máscara numérica usa `*1` para
notices de depuración UDB.

---

## 2. Protocolo S2S (Server-to-Server)

El protocolo UDB se integra en el tráfico S2S nativo de UnrealIRCd utilizando el comando extendido `DB`.
Los peers directamente enlazados que completan explícitamente el intercambio
HEL de UDB tienen capacidad de protocolo UDB V4. La capacidad no autoriza el
acceso a datos: las importaciones staged `BEGIN`, `PUT` y `END`, y las
peticiones y exportaciones `RES`, se aceptan del peer directo seleccionado
como propagador o durante el auto-bootstrap de un nodo limpio. Esto permite propagación A-a-B-a-C local por
enlace cuando B selecciona A y C selecciona B, así como arquitecturas de Ingest Gateway donde un Hub aísla a los Servicios del resto de la red. Las mutaciones en caliente deben
proceder igualmente de la autoridad configurada; un peer que sirve una
sincronización autorizada solo puede enviar registros de ese bloque.

Para garantizar la integridad y coherencia de los datos en toda la red, se recomienda
configurar el bloque `require module { name "third/udb"; };` en `unrealircd.conf`. Esto
hace que el servidor aborte de forma inmediata (`SQUIT`) cualquier intento de enlace
con un nodo que no tenga UDB activo durante el handshake inicial `SMOD`.

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
se aborta automáticamente mediante `SQUIT` para proteger la red de desincronizaciones.
`HEL` es el único frame DB
aceptado antes de confirmar y nunca se reenvía fuera del enlace directo.

**HEL (Negociación de capacidad y Auto-Bootstrap):**
`:<sid> DB <sid-peer-directo> HEL 4 <propagador-seleccionado>`

El campo de propagador seleccionado es `?` cuando no existe una fuente local
configurada. Permite al nodo nuevo auto-descubrir la autoridad y autoriza al peer directo a proveer el snapshot inicial staged.

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
explícito. Los registros persistidos deben tener componentes de ruta `::` no
vacíos y caber en el límite de ruta. UDB registra y omite líneas persistidas
malformadas o demasiado largas y continúa cargando el resto del bloque. El
receptor construye un árbol aislado por bloque y no aplica efectos
en tiempo real durante la transferencia. En `END` valida el digest canónico,
persiste el árbol staged atómicamente en el archivo temporal y solo entonces
reemplaza el árbol activo. Una desconexión del peer, 60 segundos sin actividad,
un `PUT` inválido, un txid inesperado o un digest incorrecto descarta solo el
árbol staged; el árbol activo y durable anterior no cambian.
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
Un fallo al sincronizar el directorio se informa después de que el reemplazo sea
visible, pero antes de confirmar su durabilidad ante un fallo; los snapshots y
bloques activos siempre permanecen bajo el directorio de base de datos
configurado.

**DRP (Drop / Vaciar Bloque):**
`:<sid> DB * DRP <letra_bloque>`

**OPT (Optimizar):**
`:<sid> DB * OPT <letra_bloque>`

### 2.3 Manejo de Errores (ERR)
`:<sid> DB <destino> ERR <subcomando> <codigo_error> <extra>`
*   `1`: UDB_ERR_NO_BLOCK (El bloque especificado no existe)
*   `2`: UDB_ERR_PARAMS (Parámetros insuficientes o inválidos)
*   `3`: UDB_ERR_FATAL (Error interno fatal o fallo de persistencia)
*   `4`: UDB_ERR_SYNC_ACTIVE (Ya hay una sincronización en curso)
*   `5`: UDB_ERR_NO_SYNC (No se ha solicitado una sincronización)
*   `6`: UDB_ERR_FORBIDDEN (Acción denegada por permisos / no es propagador)

### 2.4 Redacción De Secretos En DBQ

`DBQ` requiere privilegios de oper y nunca devuelve el valor de `pass`,
`challenge` ni `encryption_key`. Las consultas directas y los listados de hijos
muestran `<redacted>` para esos registros.

---

## 3. Créditos y Licencia

**Autor y Desarrollador Principal:**
El módulo UDB, su arquitectura modular moderna para UnrealIRCd 6 y las extensiones del protocolo v4 son desarrollados y mantenidos por **David Abuín Fontán ('davidlig')** (<https://github.com/davidlig/unrealircd-udb>).

**Concepto e Idea Original:**
Basado en el concepto original del protocolo UDB concebido por **Trocotronic** (*www.redyc.com*).
