# UDB — Documentación técnica

> Este documento describe la implementación canónica actual de `src/`. La versión del módulo es independiente de HEL 4 y de la generación 4 del almacenamiento nativo. Los requisitos de despliegue y las limitaciones vigentes se documentan a continuación; consulta la [guía de pruebas](../tests/README.md) para conocer el alcance de la validación.

## 1. Alcance

UDB (Unreal DataBase) es un módulo global para UnrealIRCd 6 que mantiene y aplica una base de datos distribuida para registros de nick, canales, políticas por IP, ajustes, opciones por servidor y sanciones. La versión del módulo es **4.0.0-rc1** y el metadato de distribución declara **UnrealIRCd 6.2.x** como versión mínima (`min-unrealircd-version "6.2.*"`).

La implementación canónica está dividida en `src/` y se amalgama de forma determinista en `dist/udb.c`. El módulo se registra como `third/udb` y utiliza el comando S2S `DB` como protocolo propio.

UDB no implementa un servicio de registro autónomo para crear cuentas o canales desde IRC. Las escrituras autorizadas llegan por el protocolo `DB` desde la autoridad seleccionada; el IRCd aplica esas mutaciones, las persiste y las retransmite cuando corresponde.

## 2. Arquitectura

`src/udb.c` compone una unidad de traducción. `udb_storage*.c.inc` implementa el
motor binario nativo; `udb_native*.c.inc` aporta schema capturado, consultas tipadas,
codec de protocolo, envío/recepción staged y efectos sobre generaciones exactas.
`udb_store.c.inc` sólo contiene rutas/clasificación de secretos.
`udb_core.c.inc` contiene schemas de negocio y parsing puros. Las unidades de
configuración, servicios, HEL/OCL, mutaciones, protocolo, consumidores N/C/I/K,
diagnósticos y ciclo de vida coordinan el daemon en foreground. `dist/udb.c` se genera.
READY pertenece a los seis bloques N/C/I/S/L/K, no a una raíz aislada.

## 3. Modelo de datos y rutas

Las rutas lógicas usan `Block::componente::propiedad`, por ejemplo `N::alice::pass`,
`C::#chat::access::alice`, `I::203.0.113.20::clones` y `S::propagator`.
Los componentes codifican `:`, `%`, espacios/control y bytes no ASCII como `%HH`
mayúsculo; los imprimibles `@`, `/` y `*` permanecen literales. Se rechazan controles
estructurales y NUL aunque estén escapados. Las claves pliegan ASCII salvo patrones
K::F exactos. La fila binaria distingue NONE/STRING/uint64. El protocolo usa
`!n`, `!s<Base64>` y `!u<decimal>` como etiquetas explícitas de tipo.
Las tablas siguientes describen valores lógicos, no payloads de protocolo.

| Tipo lógico | Valor wire | Ejemplo |
|---|---|---|
| NONE (entrada sin valor) | `!n` | `!n` |
| Entero sin signo (uint64) | `!u<decimal canónico>` | `5` → `!u5` |
| STRING (bytes de cadena) | `!s<Base64 RFC4648 canónico>` | `Hello world` → `!sSGVsbG8gd29ybGQ=` |

`!n` es una etiqueta literal, no un número ni un borrado. `!u0` es un valor
numérico; `!s` es una cadena vacía a nivel de codec, pero cada propiedad puede
rechazarla. No hay un tipo BYTES separado: STRING conserva los bytes suministrados
sin convertirlos (para texto UTF-8, codifica sus bytes UTF-8). Se rechazan NUL, CR
y LF y el esquema de cada propiedad impone restricciones adicionales. Base64 no
es cifrado. Los números viajan en decimal canónico, incluidas las máscaras de
bits. El `:` anterior al payload introduce el último
parámetro IRC y no forma parte del valor tipado. Las rutas usan `%HH`, no Base64;
consulta [los ejemplos completos para servicios](#104-ejemplos-s2s-para-servicios-irc).

### 3.1 Límites de representación

- Hasta 8 componentes estructurales; los schemas actuales admiten hasta 3.
- Ruta lógica raw menor de 8192 bytes; componente raw de hasta 4608 bytes.
- Valor de hasta 4096 bytes; txid de snapshot de hasta 31 caracteres.
- Patrón spamfilter decodificado de hasta 3072 bytes.
- Cada fila transmitida debe caber además en MAXLINELENGTH menos 256 bytes
  reservados para S2S; la expansión percent/Base64 cuenta para ese límite.
- Counts y números usan uint64 con aritmética comprobada y codificación LE explícita.

## 4. Esquema de los seis bloques

### 4.1 Bloque N — Nicks

Formato general:

```text
N::<nick>::<clave>
```

Claves admitidas:

| Clave | Tipo | Efecto actual |
|---|---|---|
| `access` | texto | Lista de CIDR separada por comas/espacios desde la que se permite usar el nick; nunca es identidad. Si falta, no restringe por IP. |
| `pass` | texto | Hash de contraseña y única credencial que puede establecer identidad UDB. |
| `vhost` | texto | Vhost aplicado al usuario identificado. |
| `forbid` | texto | Impide el uso del nick; en hot-sync puede forzar renombre. |
| `suspend` | texto | Con `pass`, permite el nick tras autenticar pero no asigna account/`+r` ni efectos UDB; sin `pass`, no conserva autenticación. |
| `oper` | texto | Nombre de operclass local a conceder. |
| `modes` | texto | Modos de usuario válidos; `o` está expresamente prohibido aquí. |
| `snomasks` | texto | Expresión de snomasks a aplicar (relativa `+...`, `-...` o mixta `+c-k`; letras sueltas se tratan como adición relativa `+...`). |
| `swhois` | texto | SWHOIS administrado por UDB. |

Contraseñas aceptadas:

```text
argon2id:$argon2id$...
sha256:<64 hex>
crypt:<hash>
```

Los tipos admitidos por `N::pass` son `argon2id`, `sha256` y `crypt`; su prefijo selecciona el algoritmo de autenticación. En SHA-256 se compara el SHA-256 hexadecimal de la contraseña enviada. `argon2id` es el formato recomendado para credenciales nuevas; `sha256` y `crypt` se mantienen por compatibilidad y deben migrarse en lugar de aprovisionarse para contraseñas nuevas. Las conexiones de cliente que envíen `/NICK nick:Password` o `/GHOST` deben usar TLS porque la contraseña proporcionada está presente en el comando IRC.

El control de fallos de contraseña usa una tabla de 256 entradas indexada conceptualmente por perfil/IP. El valor por defecto es `5:60` y puede configurarse con `udb::password-flood` o sustituirse en runtime mediante `S::flood`. Las entradas caducadas se reutilizan, pero una tabla activa llena falla de forma cerrada para una pareja perfil/IP nueva en vez de expulsar una entrada activa y permitir intentos de spray.

#### Uso del nick

La existencia de un perfil N no identifica a su ocupante. Un perfil sin `N::pass` no está protegido por contraseña: el nick puede usarse si `forbid` y `access` lo permiten, pero nunca concede `account=<nick>`, `+r`, vhost, operclass, modos, SWHOIS ni snomasks. Esos registros configurados siguen siendo válidos e inactivos hasta que el perfil tenga `pass` y el usuario se autentique correctamente. Mientras están dormidos, UDB no los trata como política negativa ni retira estado equivalente aportado por otra fuente, tampoco cuando se borra un registro dormido o un snapshot completo de N reemplaza o elimina el perfil.

Para un nick registrado, el cambio se autentica con:

```text
/NICK alice:Password
```

La validación de contraseña es un paso de preflight. Una credencial válida sólo queda ligada al cambio de nick pendiente: la cuenta, `+r` y los efectos del perfil se activan cuando el cambio se confirma, y ninguna credencial pendiente publica identidad por sí sola. La contraseña enviada es de un solo uso y nunca pasa a ser estado de sesión. Si el cambio falla (`433`, Q-line, límites de cambio de nick, colisión), la identidad UDB activa del nick actual no cambia. La credencial pendiente se descarta cuando cambia la política `pass`/`access` del destino, desaparece el perfil o comienza otro intento, y una credencial de un intento que no estableció el nick nunca puede reutilizarse por un rename forzado posterior. El registro inicial sigue la misma regla, así que un primer `/NICK alice:Password` activa la misma identidad que un cambio de nick posterior. Cuando un perfil antes passless recibe su primer `N::pass`, el `account`/`+r` aportado por otra fuente nunca se acepta como su credencial: sólo una autenticación UDB real autoriza identidad y efectos del perfil.

Un cambio de nick forzado por servicios no sustituye la autenticación UDB: si el perfil contiene `pass`, UDB sólo materializa identidad y efectos cuando existe una identidad activa válida generada por su propio flujo de autenticación. Un cambio forzado hacia un nick protegido sin ella se renombra de forma segura.

Si el nick está ocupado:

```text
/NICK alice!Password
```

valida contraseña + `access` y expulsa al ocupante antes de tomar el nick; sólo se aplica a un perfil con `pass`. También existe:

```text
/GHOST alice Password
```

Si el perfil contiene `access`, la contraseña correcta **no basta**: la IP del cliente debe coincidir con al menos uno de los CIDR configurados. Sin `pass`, `access` sólo restringe el uso del nick; no autentica ni permite ownership de recovery/ghost.

En un perfil normal con `N::pass`, una autenticación correcta asigna `account=<nick>` y `+r` y habilita vhost, operclass, modos, SWHOIS y snomasks. Cuando el usuario abandona el perfil, UDB retira el estado que posee, incluido el oper concedido por UDB. Al borrar `pass`, retira inmediatamente identidad/efectos UDB; ni coincidir con el nick ni un account/`+r` residual sustituyen una autenticación por contraseña.

En un reemplazo caliente del bloque N no se confía ni en el account ni en `+r`: la continuidad exige una identidad UDB activa para ese nick, que el perfil candidato conserve `pass` y que el `access` candidato siga permitiendo al cliente. Cambiar el valor de `pass`, o un `access` que aún permite, no revoca la identidad. De lo contrario UDB retira los efectos que realmente poseía y, si el ocupante carece de identidad válida y existe contraseña, renombra al ocupante actual del nick.


#### Estado runtime y ownership del bloque N

El bloque N separa tres planos y nunca infiere uno desde otro:

| Plano | Significado | Dónde vive |
|---|---|---|
| Autenticación | El cliente demostró `pass` + `access` para un nick | Credencial pendiente de un solo uso, consumida por el cambio de nick confirmado |
| Identidad | El cliente posee ahora la identidad del perfil y su proyección pública (`account=<nick>`, `+r`) | Marcador `UdbNickIdentity` |
| Ownership de efectos | Exactamente qué estado runtime (`vhost`, modos, snomasks, SWHOIS, oper) cambió UDB | Marcador `UdbNickEffects` |

Los records del perfil son **estado deseado**: lo que UDB quiere ahora. El marcador de ownership es **estado aplicado**: lo que UDB cambió realmente. La limpieza nunca lee el perfil para decidir qué retirar: el perfil actual no puede describir el pasado porque otra fuente pudo reemplazar un valor que UDB aplicó antes.

Ownership por efecto:

- **Modos**: UDB registra sólo los bits que cambió de 0 a 1. La revocación limpia exactamente esos bits, así que un modo que ya estaba activo antes de autenticar (y que también figura en `N::modes`) sobrevive. Un bit que UDB activó se posee hasta el fin de la identidad aunque otra fuente vuelva a activar el mismo bit; UnrealIRCd no tiene referencia por fuente para un bit global de modo. Es una limitación conocida y documentada.
- **Vhost**: si el vhost deseado ya está activo, UDB no reclama nada. En caso contrario registra el valor aplicado. Al revocar sólo lo retira mientras el vhost actual siga siendo el aplicado; un reemplazo de otra fuente se preserva.
- **Snomasks**: `N::snomasks` admite expresiones relativas (`+...`, `-...` o mixtas `+c-k`, así como letras sueltas tratadas como adición relativa `+...`). UDB las valida con los caracteres de snomask de UnrealIRCd y las aplica mediante la interfaz nativa `set_snomask()`. Registra la expresión aplicada y el estado previo en `UdbNickEffects`. Al revocar (o reconciliar el perfil), UDB calcula y aplica la expresión inversa (`+` pasa a `-` y `-` pasa a `+`), o restaura la máscara previa, sin borrar snomasks configuradas externamente. Los snomasks modificados externamente durante la sesión se preservan salvo cuando UDB retira un oper de su propiedad, caso en el que la limpieza nativa de deoper de UnrealIRCd elimina los snomasks.
- **SWHOIS**: UDB posee únicamente las entradas con owner `udb`.
- **Oper**: UDB respeta estrictamente el estado de oper externo: si el usuario ya es operador (`IsOper(client)`), UDB nunca lo reemplaza, degrada ni asume su propiedad. UDB sólo concede la `operclass` configurada si el cliente no era oper, marcándolo con `udb_oper_owned`. La concesión usa los defaults globales nativos de UnrealIRCd (`set::modes-on-oper`, `set::snomask-on-oper`, `set::oper-vhost` y `set::oper-auto-join`); un `N::vhost` explícito tiene prioridad y se conserva como valor literal del perfil, no como plantilla de oper expandible `ident@host`. En la revocación, suspensión, borrado de perfil o cambio de nick, UDB sólo retira el oper si era de propiedad UDB (`udb_oper_owned`); un oper externo nunca se toca. La revocación ejecuta la limpieza nativa de deoper y sólo retira/restaura un oper vhost cuando la concesión UDB lo cambió realmente y el valor aplicado no fue sustituido después por una fuente externa. Los canales unidos mediante `set::oper-auto-join` no se abandonan al hacer deoper, igual que en UnrealIRCd.

La revocación de identidad se limita a `account`, `+r` y el marcador de identidad, y sólo actúa cuando UDB posee una identidad. La revocación de efectos se limita al estado registrado en el marcador de ownership. Un perfil passless nunca crea marcador de ownership, así que "passless nunca aplica ni retira" surge del modelo en lugar de ramas especiales.

Después de que una adopción explícita mediante `/NICK nick:Password` materialice correctamente la identidad y sus efectos, UDB envía `You are now identified for nickname <nick>.` como NOTICE de NickServ. Los refresh del perfil y las adopciones suspendidas no emiten ese aviso de éxito.

Las mutaciones en caliente (`INS`/`DEL`/`UPDATE`) y los snapshots completos de N usan el mismo reconciliador: retirar efectos poseídos que ya no se desean, preservar efectos externos que UDB nunca poseyó, aplicar los efectos deseados que faltan y registrar exactamente lo que cambió. Un snapshot que conserva `pass` y un `access` que aún permite al portador mantiene la identidad y reconcilia efectos, aunque cambien los valores; un `suspend` nuevo, una eliminación, la pérdida de `pass` o una denegación de acceso revocan identidad y efectos. `INS suspend` revoca efectos e identidad pero mantiene el nick; retirar `suspend` nunca restaura la identidad.

Un `account`/`+r` externo nunca autentica. Mientras hay identidad UDB activa, un cambio externo de esos valores invalida la representación pública pero no puede recrear autenticación; al revocar la identidad se fija `account=*` y se retira `+r`. Cuando UDB nunca poseyó identidad, el `account`/`+r` externo no se toca.

`N::forbid` es exclusivo: insertarlo elimina atómicamente todas las propiedades hermanas, y no se puede insertar otra hasta borrar `forbid`. El override normal de NICK muestra el motivo sin un 432 duplicado.

Con `N::suspend` y `pass`, la validación requerida de `pass`/`access` sigue siendo obligatoria antes de adoptar el nick. El adoptante mantiene el nick pero no recibe account, `+r`, oper, vhost, modes, snomasks ni SWHOIS, y no conserva identidad alguna: `suspend` destruye cualquier identidad UDB activa. Añadirlo a un usuario identificado retira esos efectos UDB y mantiene el nick. Al retirar `suspend` de un perfil con `pass`, UDB nunca restaura la identidad: el ocupante actual se renombra y recibe de NickServ los avisos `The nickname <nick> is no longer suspended.` y después `You have been renamed. If you are the owner, please identify: /NICK <nick>:Password` antes de autenticarse de nuevo. Un perfil suspendido sin `pass` no tiene identidad, por lo que quitar `suspend` mantiene a su ocupante y nunca lo identifica. Account/`+r` por sí solos no pueden crear identidad. UDB no añade ni retira el `+S` de propiedad externa.

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
| `modes` | texto | Modos de canal y parámetros validados contra los handlers cargados. El marcador de registro `r`, los rangos de miembro (`q`, `a`, `o`, `h`, `v`), `P` y `O` se rechazan en mutaciones, sync staged y ficheros persistidos; la persistencia y la política sólo-oper se expresan únicamente con sus bits de `options`. |
| `topic` | texto | Topic gestionado por UDB. |
| `access` | contenedor | Lista de nicks autorizados. |
| `forbid` | texto | Rechaza el JOIN con el motivo almacenado. |
| `suspend` | texto | Suprime el comportamiento de canal registrado/fundador: mientras existe retira el `+r` nativo y el `+q` del fundador; al levantarlo restaura ambos en un canal vivo. El valor almacenado se muestra como motivo a cada usuario local que entra al canal mientras está suspendido. |
| `options` | numérico | Máscara de bits descrita abajo. |

`C::<canal>::access::<nick>` acepta NONE, un valor numérico o texto no vacío por esquema, pero **el hook de JOIN actual sólo comprueba la existencia de la entrada y que el usuario tenga `+r`**. El valor de la entrada no se interpreta como rango en esta implementación.

Opciones (`options`):

| Bit | Valor | Nombre | Comportamiento |
|---|---:|---|---|
| `0x01` | 1 | `PROTECT_BANS` | Un usuario normal no puede retirar un ban local creado por otro usuario; fundador y oper quedan exentos. |
| `0x02` | 2 | `LOCK_MODES` | Bloquea cambios locales de modos excepto listas `b`, `e`, `I` y rangos de miembro `q`, `a`, `o`, `h`, `v`. |
| `0x04` | 4 | `LOCK_TOPIC` | Bloquea cambios locales de topic. |
| `0x08` | 8 | `PERSISTENT` | Aplica `+P` si el modo nativo `P` existe; UDB no lo emula si falta. |
| `0x10` | 16 | `OPER_ONLY` | Restringe el JOIN a operadores IRC y aplica el `+O` nativo cuando existe su handler. El JOIN sigue fail-closed si falta. |
| `0x20` | 32 | `SECURE_OPS` | Sólo un miembro `+q` puede conceder rangos, salvo la recuperación aislada del propio rango del fundador descrita abajo. Los demás pueden retirar rangos inferiores al máximo propio, o su propio rango cuando UnrealIRCd lo permite. `SAMODE` conserva el acceso administrativo nativo. |

Los bits se combinan mediante OR bit a bit; el valor lógico `63` activa los seis (wire `!u63`). Sin `SECURE_OPS`, los permisos nativos sobre rangos no cambian salvo la recuperación del propio rango del fundador descrita abajo. `suspend` retira el `+O` propiedad de UDB y levantarlo restaura `+O`; borrar `options` o el perfil lo retira. UDB siempre reconcilia `+O/-O` antes de `+P/-P`, porque retirar `+P` puede destruir un canal vacío.

#### JOIN, fundador y clave nativa

El fundador sólo se considera identificado si su nick coincide con `founder` y tiene `+r`. Un fundador identificado se salta las restricciones de JOIN, incluidos `OPER_ONLY` y `+k`, salvo que el perfil tenga `suspend` o `forbid`; recibe `+q` salvo que esté suspendido. Un operador IRC se salta todas las restricciones JOIN de UDB, incluidos `forbid`, `OPER_ONLY` y el `+k` almacenado para el primer JOIN, pero las restricciones nativas independientes siguen ejecutándose y exigen sus propios permisos de override. `suspend` conserva su comportamiento actual y no bloquea el JOIN por sí solo.

Si la identificación ocurre después del JOIN, `/MODE #canal +q nickname` permite al fundador local identificado recuperar su propio `+q` sin rejoin. UDB exige pertenencia al canal, un perfil C no suspendido, el nick actual del fundador con el modo de usuario `+r` y exactamente el modo `+q` aislado con un parámetro que resuelva al emisor. La recuperación funciona con o sin `SECURE_OPS` y `LOCK_MODES`; repetirla cuando ya se tiene `+q` no emite otro cambio de modo. Otros destinatarios, comandos compuestos o malformados y `SAMODE` mantienen su tratamiento actual. Retirar una suspensión de nick sigue exigiendo reautenticación; este comando nunca autentica al usuario.

UDB retira el `+O` nativo sólo si fue quien lo aplicó. Si se descarga el handler nativo, UDB borra su propiedad antes de que UnrealIRCd retire directamente el modo; cuando el handler vuelve, los canales con `OPER_ONLY` activo recuperan `+O`. Un `+O` independiente, incluso uno establecido después de que otro actor retire el `+O` de UDB o después de que vuelva el handler, sobrevive a cambios ajenos de `options` y a la baja de opciones UDB.
Con un `+O` propiedad de UDB, cualquier operador IRC puede entrar aunque su operclass carezca del permiso `channel:operonly:join` de UnrealIRCd; las demás comprobaciones JOIN nativas siguen ejecutándose. Un `+O` nativo independiente conserva la exigencia de ese permiso de operclass.

El `+r` nativo es el marcador de canal registrado propiedad de UDB: registrar un canal vivo lo aplica, `suspend` y la baja del perfil lo retiran, y levantar `suspend` lo restaura junto con el `+q` del fundador. `C::modes` nunca almacena `r`, rangos de miembro, `P` ni `O`.

Antes de que el primer JOIN materialice `C::modes`, UDB aplica los `+i`, `+R`, `+z` y `+k` almacenados con sus numerics nativos y comparación exacta de clave. Los JOIN posteriores usan los handlers nativos de UnrealIRCd. `/INVITE` y un `+I` materializado conservan su semántica nativa, pero una invitación no puede eludir `forbid` ni `OPER_ONLY` de UDB.

`O` deja de ser válido en `C::modes`. No hay migración automática: convierte cualquier registro existente `C::modes +O` al bit `0x10` `OPER_ONLY` antes de cargar esta versión.

Los bans protegidos se rastrean en memoria por canal/ban/propietario. Ese propietario es estado runtime, no un registro persistente del bloque C.

### 4.3 Bloque I — IPs

Formato:

```text
I::<ip-o-realhost>::clones <uint64>
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

#### Privacidad del host y vhosts personalizados

UDB usa el host derivado `.virtual` como cloak base (`+x`, sin `+t`), no como vhost retirable. Los hosts explícitos `N::vhost`, `I::host` y de oper son capas adicionales (`+xt`); `-t` vuelve a la base protegida. Un usuario local puede recuperar su `N::vhost` actual con `+t` sólo mientras esté autenticado por UDB, con `+r`, contraseña/acceso vigentes y sin suspend/forbid. Un `+r` externo no basta. Siguen aplicándose las restricciones nativas de cambio de host y los límites de flood. El usuario puede retirar voluntariamente su propio `+x` mediante `MODE` o `UMODE2`, mostrando su host real; `+x` recupera la base UDB conservada, no el cloak nativo ni el vhost N. La autorización nativa de UnrealIRCd controla los cambios de otros emisores, incluidos servicios; UDB no añade restricciones a `-x`. Actualizar la base (también durante una recarga) conserva el estado actual de `x`; materializar un vhost personalizado del perfil sigue activando `+xt`.

La desconexión nunca cambia la visibilidad elegida: los usuarios ocultos siguen ocultos y el `-x` voluntario sigue siendo una exposición explícita. Realhost/IP siguen disponibles internamente para matching y diagnósticos autorizados. Si desaparece la clave/sufijo de derivación, los usuarios existentes conservan su base protegida; los nuevos usan un cloak nativo validado o se rechazan si no existe uno seguro. Los hosts base se sincronizan como ModData temprano de cliente, propiedad de su servidor de origen, sin claves ni cambios al protocolo DB.

UDB puede recargarse, pero no descargarse en caliente. Actualiza todos los IRCd participantes. Al sustituir el módulo instalado, es preferible un reinicio controlado. Estas protecciones afectan al host público mostrado a usuarios normales, no al transporte entre servidores de confianza ni a diagnósticos autorizados de realhost/IP.

`N::modes` no controla `x` ni `t`; el estado del host se gestiona por separado.

### 4.4 Bloque S — Settings

El bloque S sólo admite claves de profundidad 1:

| Clave | Tipo | Uso actual |
|---|---|---|
| `clones` | numérico | Límite global de clones usado como fallback por el bloque I. |
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
L::<servername>::options <uint64>
```

Sólo existe el bit:

```text
0x01 = DEBUG
```

El modo debug local se consulta específicamente en `L::<me.name>::options`. Al activarlo se habilita el flujo de debug de UDB y se retira el filtro que normalmente evita duplicar logs UDB hacia destinos snomask/oper. Cuando el módulo se descarga (por ejemplo durante un REHASH), el filtro se reaplica durante toda la ventana de descarga aunque DEBUG esté activo, porque el core puede haber descargado ya ModData de otros módulos; los destinos de log a fichero no se ven afectados.

Cualquier bit desconocido hace que el efecto se ignore con warning.

### 4.6 Bloque K — Lines

Tipos raíz admitidos:

- `G`: G-Line / server ban global.
- `Z`: Z-Line global.
- `S`: Shun global.
- `Q`: name/Q-Line global.
- `F`: spamfilter global.

Para G y S el patrón usa obligatoriamente la máscara `<user>@<host>`. Z sólo acepta direcciones IP o redes CIDR canónicas (sin hostname, usuario, alias ni bits de host en CIDR); Q acepta una máscara name-ban nativa. Cada perfil tiene una única representación:

```text
K::G::<user@host>::reason <texto>
K::Z::<ip-o-red>::reason <texto>
K::S::<user@host>::reason <texto>
K::Q::<mascara-nick>::reason <texto>
K::<tipo>::<patron>::expires <timestamp_unix>
```

El nodo patrón es un contenedor y no tiene valor directo. `expires` es un único timestamp Unix absoluto elegido por el origen; su ausencia es la única representación de una línea permanente. `expires = 0` es inválido. Un timestamp igual o anterior a la hora local nunca se materializa como TKL.

La expiración no se deriva del estado runtime de la TKL. La autoridad raíz barre perfiles K vencidos y realiza el `DEL K::<tipo>::<patron>` transaccional canónico, que elimina el subtree completo de la raíz activa y de `udb_K.db`; los followers sólo eliminan su TKL runtime local y envían o retransmiten una solicitud compare-and-delete `EXP <path> <expected-expires>` hacia su upstream seleccionado. Por tanto restart, reload, snapshot, reconnect o un cambio de una propiedad del perfil no pueden renovar ni resucitar una línea temporal. Sólo un `INS ...::expires` explícito cambia su vida; `DEL ...::expires` convierte un perfil restante válido en permanente.

UDB etiqueta las TKL gestionadas con el marcador reservado `set_by="UDB:managed"` y sólo elimina líneas propias coincidentes.

Spamfilter F es un perfil Spamfilter dinámico nativo. Su patrón se almacena **siempre** en Base64 RFC4648 canónico (prefijo `b64:`); se rechazan patrones raw. El patrón decodificado es byte-exacto y case-sensitive como identidad, está limitado a 3072 bytes y no puede contener NUL:

```text
K::F::<patron-b64>::match-type <regex|simple>
K::F::<patron-b64>::targets <letras-target-canonicas>
K::F::<patron-b64>::action <accion-dinamica>
K::F::<patron-b64>::ban-time <segundos>
K::F::<patron-b64>::reason <texto>
K::F::<patron-b64>::expires <timestamp_unix>
```

`match-type` es explícito: `regex` compila PCRE y `simple` usa el matcher nativo de wildcards de UnrealIRCd. `targets` debe usar el orden canónico nativo `cpnNPqduatTR` (channel/private/private-notice/channel-notice/part/quit/dcc/user/away/topic/message-tag/raw), sin duplicados. `action` sólo se admite cuando UnrealIRCd la reconoce como dinámica y no config-only. `ban-time` es opcional, positivo y es la duración de la sanción generada por un match; es distinto del `expires` de la regla. Un perfil F parcial es inerte de forma segura; un candidato completo inválido se rechaza antes de persistirse y no puede sustituir una regla activa.


### 4.6.1 IPv4, IPv6, CIDR y codificación de rutas UDB

Esta sección trata una dirección IPv4/IPv6 **como valor de una política del bloque K**. No describe listeners IPv6, sockets, transporte S2S, direcciones del servidor ni la configuración `listen {}`.

#### Identidad lógica, componente UDB y ruta wire

`::` separa los componentes de una ruta UDB. Por ello `:` está reservado dentro de un componente. El codec actual codifica `:`, `%`, bytes de control/espacio y bytes no ASCII; emite escapes hexadecimales en mayúsculas. Mantiene literales los caracteres imprimibles `@` y `/`. Por tanto, cada `:` de una IPv6 se escribe como `%3A`; `@` y `/` no necesitan escaparse en las formas mostradas.

Estas son tres representaciones diferentes del mismo valor:

```text
Identidad lógica:   2001:db8::1
Componente UDB:     2001%3Adb8%3A%3A1
Ruta de protocolo:  K::Z::2001%3Adb8%3A%3A1::reason
```

El percent-encoding pertenece sólo a la ruta UDB. UDB decodifica el componente antes de validar y materializar la TKL, por lo que UnrealIRCd recibe `2001:db8::1`, no `%3A`. Una IPv6 literal no es válida dentro de una ruta `DB INS`/`DB DEL`: sus dos puntos se interpretarían como sintaxis de ruta (o como un `:` simple inválido).

| Caso | Valor lógico | Componente/ruta UDB |
|---|---|---|
| IPv4 Z | `198.51.100.10` | `K::Z::198.51.100.10` |
| IPv4 CIDR Z | `198.51.100.0/24` | `K::Z::198.51.100.0/24` |
| IPv6 Z | `2001:db8::1` | `K::Z::2001%3Adb8%3A%3A1` |
| IPv6 CIDR Z | `2001:db8:1234::/48` | `K::Z::2001%3Adb8%3A1234%3A%3A/48` |
| G IPv6 | `*@2001:db8::1` | `K::G::*@2001%3Adb8%3A%3A1` |
| S IPv6 | `*@2001:db8::1` | `K::S::*@2001%3Adb8%3A%3A1` |

IPv4 no contiene `:`, por lo que estas formas IPv4 normales no necesitan percent-encoding:

```text
K::Z::198.51.100.10::reason
K::Z::198.51.100.0/24::reason
K::G::*@198.51.100.10::reason
```

#### Z/GZ-Line IPv6 y CIDR

Z tiene un validador propio. Sólo admite una dirección IPv4/IPv6 o red CIDR canónica; rechaza hostname y `user@address`. UDB no normaliza la entrada Z antes de persistir. En cambio, parsea la dirección y compara el texto recibido con la salida de `inet_ntop()` de la plataforma: una grafía textual alternativa se rechaza. Así, `2001:db8::1` se acepta, mientras que `2001:0db8:0000:0000:0000:0000:0000:0001` se rechaza en vez de reescribirse. Usa la forma comprimida y en minúsculas emitida por `inet_ntop()`.

CIDR también es identidad. Z valida el rango del prefijo y rechaza bits de host en vez de enmascararlos. Por ejemplo, `2001:db8:1234:5678::1/48` se rechaza; debe enviarse `2001:db8:1234::/48`. Esta política se aplica a redes Z IPv4 e IPv6.

```text
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234::reason :!sSVB2NiBibG9xdWVhZGE=
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234::expires :!u<unix_timestamp>
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1234

:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :!sUmVkIElQdjYgYmxvcXVlYWRh
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48
```

#### G-Line y Shun con IPv6

G y S conservan la identidad `user@host`; una IPv6 pertenece a la parte `host`. El encoder de rutas UDB sólo transforma los dos puntos:

```text
:<root-SID> DB * INS <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1234::reason :!sRy1MaW5lIElQdjY=
:<root-SID> DB * INS <root-epoch> <seq> K::G::baduser@2001%3Adb8%3A%3A1234::reason :!sRy1MaW5lIElQdjYgcG9yIGlkZW50
:<root-SID> DB * DEL <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1234

:<root-SID> DB * INS <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1234::reason :!sU2h1biBJUHY2
:<root-SID> DB * DEL <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1234
```

El schema UDB actual de G/S comprueba que existan `user` y `host`, que haya exactamente un `@` y que cada componente tenga como máximo 127 bytes. Reenvía el host decodificado a la API nativa de server-ban de UnrealIRCd. **No** parsea, normaliza ni rechaza grafías IPv6/CIDR alternativas para G/S. Por tanto, un CIDR IPv6 como `*@2001%3Adb8%3A1234%3A%3A/48` pasa la validación estructural de UDB y se reenvía como `*@2001:db8:1234::/48`, pero las garantías de identidad canónica anteriores sólo se aplican a Z. Los operadores deben usar la misma grafía canónica de `inet_ntop()` para G/S y evitar aliases textuales hasta que exista canonicalización específica de G/S.

Ejemplos de protocolo DB listos para enviar:

```text
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1::reason :!sUHJ1ZWJhIFogSVB2Ng==
:<root-SID> DB * INS <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48::reason :!sUHJ1ZWJhIHJlZCBJUHY2
:<root-SID> DB * INS <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1::reason :!sUHJ1ZWJhIEcgSVB2Ng==
:<root-SID> DB * INS <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1::reason :!sUHJ1ZWJhIFNodW4gSVB2Ng==

:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A%3A1
:<root-SID> DB * DEL <root-epoch> <seq> K::Z::2001%3Adb8%3A1234%3A%3A/48
:<root-SID> DB * DEL <root-epoch> <seq> K::G::*@2001%3Adb8%3A%3A1
:<root-SID> DB * DEL <root-epoch> <seq> K::S::*@2001%3Adb8%3A%3A1
```
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
| `propagator` | un único nombre de servidor válido | no definido |
| `password-flood` | `intentos:segundos`, ambos > 0 | `5:60` |
| `cache-size` | 16 MiB..SIZE_MAX/2 bytes | 128 MiB (local) |
| `max-staged-records` | 1..10,000,000 | 10,000,000 |
| `max-staged-bytes` | 1,024..SIZE_MAX/2 bytes | 16 GiB |
| `sync-inactivity-timeout` | 1..86,400 s | 60 s |
| `sync-absolute-timeout` | 1..86,400 s | 3600 s |
| `stale-timeout` | 1..604,800 s | 300 s |
| `anti-entropy-interval` | 1..86,400 s | 1800 s |

Las directivas desconocidas son error de configuración.

### 5.1 Precedencia de propagador

La política se resuelve en este orden:

1. `udb::propagator` del `unrealircd.conf` local.
2. `S::propagator` de la base ya publicada.
3. durante arranque, el candidato `S::propagator` cargado pero todavía no publicado.
4. ausencia de política.

Hay una diferencia deliberada: `udb::propagator` valida **un servidor**, mientras `S::propagator` admite una **lista ordenada** para failover.

Un candidato remoto sólo es seleccionable si es un servidor **directamente enlazado** al nodo actual; cuando se exige disponibilidad de protocolo, también debe tener `HEL 4` confirmado. Un servidor no adyacente no se convierte en fuente de snapshots de ese nodo aunque aparezca en la topología global.

## 6. Persistencia nativa y durabilidad

La nueva disposición exclusiva es `PERMDATADIR/udb/`:

```text
udb_N.db  udb_C.db  udb_I.db  udb_S.db  udb_L.db  udb_K.db
udb_wal.db  udb_meta0.db  udb_meta1.db
```

Los seis contenedores binarios estables contienen páginas inmutables de 64 KiB
con integridad verificada, fragmentos ordenados de hasta 4 MiB, índices paginados
y catálogos copy-on-write. No hay bloques completos residentes, directorio completo
de claves, lector de snapshots de texto, migración ni decodificador compatible.
Despliega la implementación nueva en todos los nodos. No edites ni reemplaces
parcialmente estos archivos. Una copia de seguridad debe capturar todo el almacén
con el daemon detenido, no copiar bloques por separado mientras está activo.

### 6.1 Lecturas indexadas y acotadas

Las consultas exactas usan una partición de entidad entre 4096 y después un árbol
radix SHA-256 de la clave completa. Las hojas se dividen por encima de 16 MiB de
filas codificadas más 128 bytes por fila y colapsan cuando el contenido visible
queda por debajo de ese límite. Los índices de fragmentos/runs localizan los datos
sin cargar un bloque completo. Los prefijos usan iteradores de disco acotados.
Un pin de generación inmutable protege las extensiones hasta liberar el lector.
Las consultas distinguen FOUND, NOT_FOUND y errores de almacenamiento; un error
nunca significa ausencia. El cero numérico es un valor, no un campo inexistente.

La caché LRU local usa 128 MiB por defecto. `udb::cache-size` recibe bytes decimales
(mínimo 16 MiB), puede diferir entre nodos y no se replica. Por ejemplo,
`cache-size 268435456;` selecciona 256 MiB. Más caché puede retener más páginas
calientes de datos/índices y reducir I/O y validaciones repetidas, pero no elimina
la validación del arranque ni garantiza cargas masivas más rápidas. El rehash
aplica el presupuesto local; si los pins impiden reducirlo, se mantiene el tamaño
actual y hace falta otro rehash. Las asignaciones auxiliares gestionadas tienen
un límite de 64 MiB, incluidos 16 MiB de memtables y una cola total de entregas,
jobs y completados de 8 MiB. Los clientes/TKL del daemon y la caché del sistema
operativo no pertenecen a ese presupuesto de almacenamiento.

### 6.2 Commit, checkpoint y recuperación

Un único escritor nativo recibe jobs copiados con IDs estables y revisiones de
módulo/autoridad, nunca punteros `Client *` en el worker. Los grupos tienen un
objetivo de hasta 5 ms o 1 MiB de jobs en cola. Una barrera final de autorización
precede la durabilidad. El orden es flush de datos, flush del conjunto de raíces
en el WAL global, publicación inmutable y después efectos, ACK y relay en el
foreground. Los recibos fijan las raíces exactas anterior/posterior incluso cuando
ya se publicó otro grupo. Cancelar antes de la barrera impide publicar; hacerlo
después no revierte un commit durable, pero tampoco concede READY ni relay obsoletos.

Los checkpoints usan el slot de metadatos inactivo de 4 MiB, flush y verificación
por relectura, un suelo durable en el WAL y después retiran entradas cubiertas.
La reutilización de extensiones exige un inventario completo, condicionado por
checkpoint, de todas las generaciones activas, staged y fijadas. La caché incluye
identidad de archivo y generación de extensión. El mantenimiento escribe hasta
64 MiB por paso privado. Los tombstones de prefijo mantienen el orden: un PUT
posterior sobrevive a un borrado previo del subárbol. DRP publica una raíz vacía.

El arranque valida WAL/checkpoints y recorre los datos físicos y lógicos referenciados
de los seis bloques antes de servir. Cuesta CPU/I/O O(datos referenciados), no memoria
residente O(registros), y no admite páginas DATA en caché durante la validación.
Una búsqueda indexada caliente no sustituye esa comprobación. Los almacenes
corruptos, incompletos, ajenos o ambiguamente provisionados fallan de forma cerrada,
sin reemplazarse silenciosamente por datos vacíos. Sólo se retiran colas incompletas
no referenciadas del WAL después de validar las raíces comprometidas. La limpieza
de temporales huérfanos exige identidad de almacén, cabecera íntegra, propietario
e identidad del archivo abierto; preserva archivos malformados, ajenos y symlinks.
Se detiene la admisión y se espera al escritor antes de liberar pins, schema,
efectos o handles de almacenamiento.

### 6.3 Plataformas y límites de la evidencia

Linux usa I/O posicionado de 64 bits, archivos sólo del propietario, bloqueo
exclusivo y flush real. La rama Windows usa rutas wide, I/O/flush nativos, handles
exclusivos, ACL del propietario, NTFS local fijo y verificaciones de reparse/hardlinks.
El commit no depende de unlink/rename ni de simular fsync de directorios Windows.
Los objetivos son Windows 11 y Server 2022/2025 x64 con MSVC v142; las comprobaciones
Linux no prueban ejecución real MSVC/NTFS. Consulta `tests/README.md` para la evidencia
y sus límites.

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

- versión exigida: `4` (la única versión de protocolo aceptada);
- `epoch`: 16 caracteres hexadecimales minúsculos que identifican la instancia runtime del peer;
- `OCL`: obligatorio; capacidad de operclasses distribuidas en runtime;
- `OCLG`: opcional; indica suscripción a la proyección global, normalmente usada por consumidores como Services.

El selector anunciado significa:

- `?`: nodo todavía sin política/READY y dispuesto a que ese vecino sea su dueño exclusivo de bootstrap;
- `-`: existe política pero no hay candidato utilizable;
- `<servername>`: fuente/propagador seleccionado.

Si un peer directo no responde al HEL dentro del timeout, UDB aborta el enlace de servidor. Ciertas formas heredadas reconocibles que omiten o colocan mal la capacidad obligatoria `OCL` se abortan inmediatamente; otros frames HEL malformados se ignoran, no pueden confirmar la capacidad y pueden acabar alcanzando el timeout. Estas rutas no emiten un frame DB `ERR HEL`. Un cambio de epoch de instancia del SID upstream seleccionado se trata como un nuevo stream de autoridad: reinicia el estado de secuencia/stream y requiere una reconciliación completa. Para cualquier otro peer directo, el cambio sólo invalida el estado de instancia replay/OCL de ese peer.

## 8. Modelo de autoridad, bootstrap y freshness

### 8.1 Con política

El primer candidato válido de la política se selecciona. Para importar snapshots o aceptar mutaciones, el tráfico debe llegar a través del peer directo seleccionado con HEL confirmado. Una mutación multihop conserva el SID/epoch del origen raíz en su prefijo IRC y payload; el peer directo es el salto de transporte autorizado, no necesariamente el origen del stream.

Si cambia la política, UDB cancela sesiones/peticiones pertenecientes a otra fuente y vuelve a evaluar la reconciliación.

### 8.2 Sin política

- Antes de READY, el primer peer directo autorizado que actúa como fuente de bootstrap se fija como **bootstrap owner exclusivo**. Otro HEL posterior no puede robárselo.
- Tras READY, el nodo sin política se considera **autoridad standalone** y deja de aceptar imports remotos.

Esto evita que un nodo ya autoritativo adopte accidentalmente la base de un vecino sólo por estar conectado.

### 8.3 Semántica de freshness y rollback (Política A — Autoridad absoluta)

UDB opera bajo un modelo de autoridad explícito y determinista (**Política A — Prevalencia del propagador seleccionado**):

1. **Precedencia absoluta de la autoridad**:
   - El propagador autoritativo seleccionado es la única fuente de verdad para el estado de la base de datos replicada.
   - Durante la reconciliación, cuando un snapshot autoritativo anuncia un digest SHA-256 canónico divergente, el nodo receptor descarga, verifica y confirma incondicionalmente el snapshot de la autoridad, reemplazando el estado de su bloque local.
   - Las mutaciones en vivo originadas por el propagador autoritativo con secuencia estrictamente consecutiva (`seq == expected_seq`) se aplican directamente sobre el estado confirmado.

2. **Semántica de rollback y sobrescritura**:
   - Dado que el estado de la autoridad seleccionada es vinculante, si el dataset de la autoridad contiene menos registros, elimina claves o refleja un estado lógico anterior al que posee una copia local divergente del follower, el follower adopta el estado de la autoridad y descarta su divergencia local.
   - Este "rollback" hacia la perspectiva de la autoridad es intencionado y esencial para mantener convergencia determinista en toda la red. Un nodo follower nunca rechaza ni falla un snapshot autoritativo alegando una supuesta "mayor frescura local".
   - En UDB no existe el concepto ambiguo de "gana el timestamp más reciente" (LWW / Last-Write-Wins basado en reloj de pared) ni resolución de conflictos multi-master. Los relojes locales jamás se consultan para dirimir conflictos.

3. **Carácter informativo de `mtime` y timestamps de reloj**:
   - Los tiempos de modificación del sistema de archivos (`st_mtime`) y el campo `modified_at` de `INF` son metadatos puramente de diagnóstico para observabilidad del operador (por ejemplo, en consultas `/DBQ <bloque>`).
   - `mtime` nunca se compara para dirimir registros en conflicto, determinar propiedad del bloque ni seleccionar qué datos prevalecen.
   - El desfase de reloj entre servidores, diferencias horarias o alteraciones de fecha en disco (e.g. `touch`) no alteran en modo alguno la sincronización, la aceptación de snapshots ni la convergencia.

4. **Desempates y fronteras en modo standalone**:
   - Los SIDs e identificadores de servidor nunca anulan una relación de autoridad configurada. Si el nodo A está configurado para seguir al nodo B, el estado de B prevalece siempre, independientemente del orden léxico de los SIDs (`SID(A) > SID(B)` no tiene efecto).
   - En despliegues sin política explícita, el primer peer directo confirmado durante el bootstrap se fija como dueño exclusivo de bootstrap. Al alcanzar `READY`, el nodo sin política se blinda como autoridad standalone y rechaza imports remotos entrantes, evitando que un vecino recién enlazado sobreescriba accidentalmente sus datos.


## 9. Reconciliación nativa por snapshots

La reconciliación usa inventario y pull entre vecinos elegibles directamente
conectados con HEL 4/OCL. Un snapshot nunca se reenvía por otro salto. MANIFEST,
INF y las filas del emisor proceden de una generación fijada de seis raíces.

### 9.1 Gramática exacta del protocolo

```text
:<peer-SID> DB <target> INF <round> <block> <sha256> <count> 0 <watermark> <root-SID> <root-epoch>
:<peer-SID> DB <target> RES <round> <block>
:<peer-SID> DB <target> BEGIN <round> <block> <txid> <sha256> <watermark>
:<peer-SID> DB <target> PUT <round> <block> <txid> <Block::path> :<typed-value>
:<peer-SID> DB <target> END <round> <block> <txid> <sha256> <watermark>
:<receiver-SID> DB <peer> ACK <round> <block> <txid> <sha256> <watermark>
```

`typed-value` es exactamente `!n` (NONE), `!u<decimal uint64 canónico>` (incluido cero)
o `!s<Base64 canónico>` (bytes de cadena). Se rechazan valores sin etiqueta e INF
sin origen. HEL sigue en 4; las formas de solicitud y ACK no cambian. Los seis INF
deben coincidir en SID, epoch y watermark del origen. El epoch HEL del vecino
identifica su instancia, no necesariamente el origen raíz de las mutaciones.
El timestamp reservado se emite como cero y no determina frescura.

### 9.2 Staging privado durable

BEGIN asigna un propietario privado en el escritor; PUT copiados alimentan un
ordenamiento externo acotado. Claves canónicas duplicadas, schema inválido,
propietario incorrecto, count/digest erróneo, replay, cuota agotada o secuencia
inválida no pueden publicar datos activos. END sella y valida una raíz aislada en
disco y la registra duramente como pendiente; sólo su recibo durable permite ACK.
Las raíces activas iguales usan REUSE durable. PROMOTE publica atómicamente seis
raíces y su origen. El ACK de un bloque no significa READY de toda la base. Los
recibos de la cola live retenida y sus efectos deben terminar bajo la misma
autoridad actual antes de READY.

Valores por defecto por bloque: 10.000.000 filas, cuota física de staging de 16 GiB
incluidos ordenamiento y salida, 60 s de inactividad y 3600 s absolutos. El envío
está acotado y respeta SendQ. Desconexión/reinicio HEL cancela el propietario sin
inventar otro origen raíz. La recuperación descarta propietarios pendientes
abandonados sólo después de validar sus raíces referenciadas.

### 9.3 Checksums lógicos

Los hashes SHA-256 separan dominios y usan filas binarias little-endian explícitas,
ordenadas por hoja canónica, vinculando posición/count/peso. Particiones de entidad,
división/colapso radix de clave completa, dominios hoja/rama/bloque y posiciones
vacías son canónicos: el hash no depende de orden de inserción, caché, IDs físicos
ni historial de compactación. Los bloques vacíos tienen entradas de digest
específicas del bloque, no SHA-256 de una cadena vacía. Las claves ASCII se pliegan
salvo el patrón exacto K::F. La referencia independiente está en
`tests/support/udb_test_support/native_reference.py`.

### 9.4 Orden snapshot/live

El import vincula el SID/epoch/watermark exacto del origen. Se descartan entradas
provisionales cubiertas y la cola retenida debe comenzar en watermark+1, sin huecos
ni orígenes incorrectos. Los envelopes de replay reservan crédito para su job
futuro dentro del mismo límite de 8 MiB: una entrada llena no bloquea su drenaje.
Promoción/efectos preceden a replay; replay/efectos preceden a READY. Una ronda
parcial u obsoleta no autoriza READY, servicio a downstream ni relay obsoleto.

## 10. Mutaciones en vivo y secuenciación

Las mutaciones autorizadas originadas por el propagador autoritativo seleccionado son:

```text
INS <epoch> <seq> <Block::path> :<typed-value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
EXP <path> <expected-expires>
```

Parámetros:
- `epoch`: 16 caracteres hexadecimales minúsculos que identifican el instance epoch del origen raíz.
- `seq`: entero decimal de 64 bits estrictamente monotónico dentro del stream `(SID de origen raíz, epoch)` (`seq >= 1`), compartido globalmente entre los seis bloques persistentes (`N`, `C`, `I`, `S`, `L`, `K`) para garantizar orden causal total.

Semántica:

- `INS`: inserta o sustituye un valor después de validar límites y esquema.
- `DEL`: elimina una ruta; borrar una ruta inexistente es idempotente.
- `DRP`: vacía un bloque completo, persistiendo primero el snapshot vacío.
- `EXP`: solicitud compare-and-delete punto a punto de una línea K expirada enviada hacia la autoridad directa seleccionada (`DB <target> EXP <path> <expected-expires>`). Un relay valida y deduplica la solicitud y después la reenvía a su propio upstream seleccionado. Sólo la autoridad raíz elimina el perfil, persiste `udb_K.db`, asigna la siguiente secuencia del stream y difunde el `DEL` transaccional; las solicitudes obsoletas se ignoran de forma segura sin error.

### 10.1 Reglas de secuencia monotónica y autorreparación de gaps

Los receptores procesan las mutaciones entrantes contra el stream activo `(SID de origen raíz, epoch)`, recibido a través del peer directo seleccionado:

- `expected_seq = last_applied_seq + 1`:
  - **En orden (`seq == expected_seq`)**: Valida -> persiste localmente -> publica en runtime -> avanza `last_applied_seq = seq` -> retransmite multihop a downstream peers confirmados.
  - **Duplicado / Obsoleto (`seq <= last_applied_seq`)**: Se descarta de forma segura sin re-aplicar ni mutar el estado activo.
  - **Hueco / Desorden (`seq > expected_seq`)**: Se detecta gap. El nodo suspende la aplicación online, marca la salud de sincronización como `DEGRADED` y dispara inmediatamente una ronda de reconciliación staged (`RES`) para sanar el hueco.
  - **Stream discordante (cambia el SID de origen o el epoch)**: El paquete no se aplica. Su stream pasa a ser candidato y se ejecuta una reconciliación completa de los seis bloques; sólo una ronda correcta promociona el candidato y adopta su watermark exacto, que legítimamente puede ser inferior al del stream anterior.
  - **Fallo de persistencia**: Si falla la persistencia local de una mutación en orden (por ejemplo, error de E/S de disco), el nodo marca salud `DEGRADED` e inicia reconciliación, asegurando que un fallo de almacenamiento local no genere divergencia silenciosa en la red.

A diferencia de `INF/RES/BEGIN/PUT/END`, las mutaciones sí están diseñadas para propagarse por múltiples saltos mediante retransmisión validada en cada nodo.

### 10.2 Verificación periódica de anti-entropía

Para detectar divergencias silenciosas entre peers sin depender de reconexiones de enlace o reinicios, los nodos sanos verifican periódicamente su estado contra su autoridad directa:

Frames:

```text
MANIFEST REQ <round>
MANIFEST ACK <round> <block> <count> <sha256> <watermark_seq>
```

Flujo:
1. Cada `anti-entropy-interval` (por defecto 1800 s ± 180 s de jitter), el follower envía `MANIFEST REQ` a su autoridad seleccionada.
2. La autoridad responde con `MANIFEST ACK` para cada uno de los seis bloques, indicando su `(count, sha256, watermark_seq)` actual. Los seis acknowledgements deben llevar un watermark idéntico; un conjunto inconsistente falla de forma cerrada y fuerza refresh/recuperación de capacidad.
3. El follower compara los manifests de la autoridad con sus manifests locales activos.
4. **Coincidencia**: Todos los bloques coinciden; el estado está verificado como convergente. No se transfieren datos ni se generan logs ruidosos.
5. **Divergencia**: Cualquier diferencia en recuento o digest SHA-256 marca inmediatamente la salud como `DEGRADED` y solicita reconciliación staged (`RES`) para el bloque divergente.
6. El rate limiting y el límite de una sola comprobación en curso previenen tormentas de sincronización.

### 10.3 Códigos de error S2S

| Código | Nombre |
|---:|---|
| 1 | `NO_BLOCK` |
| 2 | `PARAMS` |
| 3 | `FATAL` |
| 4 | `SYNC_ACTIVE` |
| 5 | `NO_SYNC` |
| 6 | `FORBIDDEN` |


### 10.4 Ejemplos S2S para servicios IRC

Este catálogo cubre todas las propiedades persistentes de N/C/I/S/L/K y los cinco
tipos K. No es una implementación de sincronización inicial ni un lote para
aplicar sin cambios: incluye privilegios de oper, sanciones y secretos públicos
de demostración. Las directivas locales de `unrealircd.conf` y OCL/OCLG, sólo
runtime, son interfaces separadas.

Supongamos que el servidor de servicios conectado es `services.example.net` (SID
`9SV`) y el IRCd directamente enlazado es `irc.example.net` (SID `001`). Los
servicios son la autoridad raíz del ejemplo, con epoch `0123456789abcdef` (16 hex
minúsculas). Su anuncio de capacidad utiliza su propio nombre de servidor como
selector. Una solicitud es:

```text
:9SV DB 001 HEL 4 services.example.net 0123456789abcdef OCL
```

Al recibir la solicitud HEL propia del IRCd, los servicios responden:

```text
:9SV DB 001 HEL 4 ACK services.example.net 0123456789abcdef OCL
```

Ambas direcciones deben confirmar HEL 4/OCL. HEL no es autenticación criptográfica
ni una concesión de autoridad de escritura; usa TLS autenticado con verificación
de certificado. El IRCd receptor debe haber seleccionado este peer directo como
autoridad y reconciliado los seis bloques, adoptando el origen raíz
`(9SV, 0123456789abcdef)` con watermark `0`. Anunciar OCL exige implementar su
contrato; el token por sí solo no implementa unos servicios. Un peer no autorizado
no puede autorizarse escribiendo `S::propagator`.

Sólo bajo esas condiciones las siguientes mutaciones en vivo empiezan en secuencia
1. Para un stream existente, continúa en su siguiente secuencia; no reinicies el
contador del ejemplo. INS/DEL/DRP comparten una secuencia uint64 consecutiva entre
los seis bloques. El prefijo es el **SID del servidor** raíz, no el UID de NickServ.
Los relays conservan SID/epoch/secuencia del origen aunque el peer directo de
transporte seleccionado sea distinto. Termina cada línea con CRLF (`\r\n`);
no envíes fences Markdown ni comentarios. Cada INS es una mutación individual,
no una transacción atómica que englobe el catálogo. Los modos/snomasks deben tener
handlers cargados y la operclass `netadmin` del ejemplo debe existir para producir
su efecto previsto. Sustituye nombres y políticas por los valores autorizados de
tu red; los selectores HEL deben reflejar el estado real del emisor.

#### N — Nicks

Sus nueve propiedades son cadenas. La entrada raíz opcional ilustra NONE.
`pass` transmite la cadena completa `argon2id:$argon2id$…`, codificada en Base64,
para la contraseña pública de ejemplo `EXAMPLE-ONLY-CHANGE-ME` y el salt
`udb-example-salt`. No reutilices estas credenciales de demostración. Los hashes
admitidos `sha256:` y `crypt:` usan el mismo envoltorio `!s`; usa Argon2id para
credenciales nuevas. `forbid` y `suspend` afectan a otros nicks de demostración,
no a `alice`.

Valor pass decodificado (antes de Base64):

```text
argon2id:$argon2id$v=19$m=19456,t=2,p=1$dWRiLWV4YW1wbGUtc2FsdA$i5Orjj4GOgK7ORVIoiTYp+9CFRLbOifSIkDenWCrFVo
```

| Ruta wire | Valor lógico |
|---|---|
| `N::alice` | `NONE` |
| `N::alice::access` | `203.0.113.0/24` |
| `N::alice::pass` | Hash Argon2id mostrado arriba |
| `N::alice::vhost` | `alice.example.net` |
| `N::Reserved::forbid` | `Reserved nick` |
| `N::Suspended::suspend` | `Suspended account` |
| `N::alice::oper` | `netadmin` |
| `N::alice::modes` | `+i` |
| `N::alice::snomasks` | `+c-k` |
| `N::alice::swhois` | `Network staff` |

```text
:9SV DB * INS 0123456789abcdef 1 N::alice :!n
:9SV DB * INS 0123456789abcdef 2 N::alice::access :!sMjAzLjAuMTEzLjAvMjQ=
:9SV DB * INS 0123456789abcdef 3 N::alice::pass :!sYXJnb24yaWQ6JGFyZ29uMmlkJHY9MTkkbT0xOTQ1Nix0PTIscD0xJGRXUmlMV1Y0WVcxd2JHVXRjMkZzZEEkaTVPcmpqNEdPZ0s3T1JWSW9pVFlwKzlDRlJMYk9pZlNJa0RlbldDckZWbw==
:9SV DB * INS 0123456789abcdef 4 N::alice::vhost :!sYWxpY2UuZXhhbXBsZS5uZXQ=
:9SV DB * INS 0123456789abcdef 5 N::Reserved::forbid :!sUmVzZXJ2ZWQgbmljaw==
:9SV DB * INS 0123456789abcdef 6 N::Suspended::suspend :!sU3VzcGVuZGVkIGFjY291bnQ=
:9SV DB * INS 0123456789abcdef 7 N::alice::oper :!sbmV0YWRtaW4=
:9SV DB * INS 0123456789abcdef 8 N::alice::modes :!sK2k=
:9SV DB * INS 0123456789abcdef 9 N::alice::snomasks :!sK2Mtaw==
:9SV DB * INS 0123456789abcdef 10 N::alice::swhois :!sTmV0d29yayBzdGFmZg==
```

#### C — Canales

El fundador es `alice`, los modos son `+nt` y el topic es `Hello world`.
El contenedor access y la entrada `bob` ilustran NONE. El hijo puede llevar como
alternativa `!u10` o `!sbWVtYmVy` (texto `member`), pero el hook de JOIN no interpreta
su valor como rango. Forbid/suspend afectan a otros canales de demostración.

| Ruta wire | Valor lógico |
|---|---|
| `C::#chat` | `NONE` |
| `C::#chat::founder` | `alice` |
| `C::#chat::modes` | `+nt` |
| `C::#chat::topic` | `Hello world` |
| `C::#chat::access` | `NONE` |
| `C::#chat::access::bob` | `NONE` |
| `C::#reserved::forbid` | `Reserved channel` |
| `C::#suspended::suspend` | `Suspended channel` |
| `C::#chat::options` | `63` |

```text
:9SV DB * INS 0123456789abcdef 11 C::#chat :!n
:9SV DB * INS 0123456789abcdef 12 C::#chat::founder :!sYWxpY2U=
:9SV DB * INS 0123456789abcdef 13 C::#chat::modes :!sK250
:9SV DB * INS 0123456789abcdef 14 C::#chat::topic :!sSGVsbG8gd29ybGQ=
:9SV DB * INS 0123456789abcdef 15 C::#chat::access :!n
:9SV DB * INS 0123456789abcdef 16 C::#chat::access::bob :!n
:9SV DB * INS 0123456789abcdef 17 C::#reserved::forbid :!sUmVzZXJ2ZWQgY2hhbm5lbA==
:9SV DB * INS 0123456789abcdef 18 C::#suspended::suspend :!sU3VzcGVuZGVkIGNoYW5uZWw=
:9SV DB * INS 0123456789abcdef 19 C::#chat::options :!u63
```

La máscara 63 del ejemplo activa los seis bits; 6 combina LOCK_MODES y LOCK_TOPIC.

| Bit | Valor | Payload wire por separado |
|---|---:|---|
| `PROTECT_BANS` | 1 | `!u1` |
| `LOCK_MODES` | 2 | `!u2` |
| `LOCK_TOPIC` | 4 | `!u4` |
| `PERSISTENT` | 8 | `!u8` |
| `OPER_ONLY` | 16 | `!u16` |
| `SECURE_OPS` | 32 | `!u32` |

#### I — Políticas por IP/realhost

El ejemplo permite cinco conexiones, guarda todos los flags admitidos de
`nolines` como cadena `GZQSTmc` y establece el host visible `shared.example.net`.
La búsqueda de esta raíz es exacta contra IP o realhost, no matching CIDR.

| Ruta wire | Valor lógico |
|---|---|
| `I::203.0.113.20` | `NONE` |
| `I::203.0.113.20::clones` | `5` |
| `I::203.0.113.20::nolines` | `GZQSTmc` |
| `I::203.0.113.20::host` | `shared.example.net` |

```text
:9SV DB * INS 0123456789abcdef 20 I::203.0.113.20 :!n
:9SV DB * INS 0123456789abcdef 21 I::203.0.113.20::clones :!u5
:9SV DB * INS 0123456789abcdef 22 I::203.0.113.20::nolines :!sR1pRU1RtYw==
:9SV DB * INS 0123456789abcdef 23 I::203.0.113.20::host :!sc2hhcmVkLmV4YW1wbGUubmV0
```

#### S — Settings

El límite global de clones es 3; el mensaje de rechazo es `Too many connections`
y la política de fallos de contraseña es el texto `5:60`. El sufijo es
`.users.example.net`. Las máscaras de servicios son las cadenas
`NickServ!service@services.example.net`, `ChanServ!service@services.example.net`
e `IpServ!service@services.example.net`; seleccionan usuarios ULine conectados
coincidentes, no introducen esos usuarios. El propagador es la lista ordenada
`services.example.net,backup.example.net`. `encryption_key` contiene
`0123456789abcdef` repetido cuatro veces: Base64 codifica esos **64 caracteres
hexadecimales**, no 32 bytes raw de clave. Esta clave pública es sólo de
demostración; no la despliegues.

| Ruta wire | Valor lógico |
|---|---|
| `S::clones` | `3` |
| `S::quit_clones` | `Too many connections` |
| `S::flood` | `5:60` |
| `S::encryption_key` | `0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef` |
| `S::suffix` | `.users.example.net` |
| `S::nickserv` | `NickServ!service@services.example.net` |
| `S::chanserv` | `ChanServ!service@services.example.net` |
| `S::ipserv` | `IpServ!service@services.example.net` |
| `S::propagator` | `services.example.net,backup.example.net` |

```text
:9SV DB * INS 0123456789abcdef 24 S::clones :!u3
:9SV DB * INS 0123456789abcdef 25 S::quit_clones :!sVG9vIG1hbnkgY29ubmVjdGlvbnM=
:9SV DB * INS 0123456789abcdef 26 S::flood :!sNTo2MA==
:9SV DB * INS 0123456789abcdef 27 S::encryption_key :!sMDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWYwMTIzNDU2Nzg5YWJjZGVmMDEyMzQ1Njc4OWFiY2RlZg==
:9SV DB * INS 0123456789abcdef 28 S::suffix :!sLnVzZXJzLmV4YW1wbGUubmV0
:9SV DB * INS 0123456789abcdef 29 S::nickserv :!sTmlja1NlcnYhc2VydmljZUBzZXJ2aWNlcy5leGFtcGxlLm5ldA==
:9SV DB * INS 0123456789abcdef 30 S::chanserv :!sQ2hhblNlcnYhc2VydmljZUBzZXJ2aWNlcy5leGFtcGxlLm5ldA==
:9SV DB * INS 0123456789abcdef 31 S::ipserv :!sSXBTZXJ2IXNlcnZpY2VAc2VydmljZXMuZXhhbXBsZS5uZXQ=
:9SV DB * INS 0123456789abcdef 32 S::propagator :!sc2VydmljZXMuZXhhbXBsZS5uZXQsYmFja3VwLmV4YW1wbGUubmV0
```

#### L — Opciones por servidor

`irc.example.net` es el IRCd cuyo efecto local de debug se selecciona. El único
bit conocido es `DEBUG = 1` (`!u1`); una máscara cero es `!u0`.

| Ruta wire | Valor lógico |
|---|---|
| `L::irc.example.net` | `NONE` |
| `L::irc.example.net::options` | `1` |

```text
:9SV DB * INS 0123456789abcdef 33 L::irc.example.net :!n
:9SV DB * INS 0123456789abcdef 34 L::irc.example.net::options :!u1
```

#### K — Líneas G/Z/S/Q y spamfilters F

Cada perfil G/Z/S/Q admite `reason` y `expires`. La expiración futura del ejemplo,
`2000000000`, es un **timestamp Unix absoluto**, no una duración. Para perfiles
temporales nuevos envía expires antes de reason: un perfil parcial sin motivo es
inerte y así se evita un efecto permanente intermedio. Los perfiles existentes
pueden estar ya activos.

La regex F es `spam[0-9]+`. Su componente almacenado es `b64:c3BhbVswLTldKw==`,
por lo que la ruta wire usa `b64%3Ac3BhbVswLTldKw==`. Los valores decodificados son
`regex`, `cp` (mensajes a canal/privados), `gline`, 300 segundos de duración de la
sanción, expiración `2000000000` de la propia regla y motivo `Spam detected`.
El motivo se envía al final para que la nueva regla siga incompleta hasta disponer
de las demás propiedades. Los nombres son exactamente **`match-type`** y
**`ban-time`**, con guiones.

| Ruta wire | Valor lógico |
|---|---|
| `K::G::*@bad.example::expires` | `2000000000` |
| `K::G::*@bad.example::reason` | `G-Line example` |
| `K::Z::2001%3Adb8%3A%3A1::expires` | `2000000000` |
| `K::Z::2001%3Adb8%3A%3A1::reason` | `Z-Line example` |
| `K::S::*@flood.example::expires` | `2000000000` |
| `K::S::*@flood.example::reason` | `Shun example` |
| `K::Q::BadNick*::expires` | `2000000000` |
| `K::Q::BadNick*::reason` | `Q-Line example` |
| `K::F::b64%3Ac3BhbVswLTldKw==::match-type` | `regex` |
| `K::F::b64%3Ac3BhbVswLTldKw==::targets` | `cp` |
| `K::F::b64%3Ac3BhbVswLTldKw==::action` | `gline` |
| `K::F::b64%3Ac3BhbVswLTldKw==::ban-time` | `300` |
| `K::F::b64%3Ac3BhbVswLTldKw==::expires` | `2000000000` |
| `K::F::b64%3Ac3BhbVswLTldKw==::reason` | `Spam detected` |

```text
:9SV DB * INS 0123456789abcdef 35 K::G::*@bad.example::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 36 K::G::*@bad.example::reason :!sRy1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 37 K::Z::2001%3Adb8%3A%3A1::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 38 K::Z::2001%3Adb8%3A%3A1::reason :!sWi1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 39 K::S::*@flood.example::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 40 K::S::*@flood.example::reason :!sU2h1biBleGFtcGxl
:9SV DB * INS 0123456789abcdef 41 K::Q::BadNick*::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 42 K::Q::BadNick*::reason :!sUS1MaW5lIGV4YW1wbGU=
:9SV DB * INS 0123456789abcdef 43 K::F::b64%3Ac3BhbVswLTldKw==::match-type :!scmVnZXg=
:9SV DB * INS 0123456789abcdef 44 K::F::b64%3Ac3BhbVswLTldKw==::targets :!sY3A=
:9SV DB * INS 0123456789abcdef 45 K::F::b64%3Ac3BhbVswLTldKw==::action :!sZ2xpbmU=
:9SV DB * INS 0123456789abcdef 46 K::F::b64%3Ac3BhbVswLTldKw==::ban-time :!u300
:9SV DB * INS 0123456789abcdef 47 K::F::b64%3Ac3BhbVswLTldKw==::expires :!u2000000000
:9SV DB * INS 0123456789abcdef 48 K::F::b64%3Ac3BhbVswLTldKw==::reason :!sU3BhbSBkZXRlY3RlZA==
```

La ruta Z de IPv6 escapa sus dos puntos con percent-encoding; los imprimibles
`*`, `@`, `/` y `=` permanecen literales donde se usan. El Base64 del patrón F
pertenece a la clave y es independiente del `!s<Base64>` de los valores de sus
propiedades. Los contenedores `K::<tipo>::<patrón>` **no** admiten INS, incluido
`:!n`. La ausencia de expires significa línea permanente; `expires = 0` es inválido.

#### Borrado y sincronización inicial

Para quitar una propiedad o un subtree, usa DEL sin valor; INS con NONE no es un
borrado. Continuando el stream del ejemplo:

```text
:9SV DB * DEL 0123456789abcdef 49 N::Reserved::forbid
:9SV DB * DEL 0123456789abcdef 50 K::Z::2001%3Adb8%3A%3A1
```

Borrar únicamente `expires` convierte el perfil K restante válido en permanente.
Para snapshots iniciales, usa el mismo payload tipado dentro de PUT en lugar de
enviar un catálogo de INS en vivo inmediatamente después de HEL. Por ejemplo,
dentro de una ronda ya autorizada y una transacción iniciada:

```text
:9SV DB 001 PUT <round> N <txid> N::alice::vhost :!sYWxpY2UuZXhhbXBsZS5uZXQ=
```

Requiere el ciclo completo INF/RES/BEGIN/PUT/END/ACK de la sección 9, incluidos los
digests exactos y la vinculación de origen/watermark de los seis bloques. Un PUT
aislado, un envío exitoso o un PONG no prueban commit durable ni READY. Gestiona ERR
y recuperación según el estado real del stream; no reutilices una secuencia para
otra mutación. Estos ejemplos se contrastan con el esquema y el codec; no afirman
pruebas de integración en vivo con un daemon externo de servicios.

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

El comando `NICK` inicial aplica el mismo rechazo de readiness antes de consultar la política de nicknames, también durante la negociación CAP. Un cliente todavía sin nickname recibe un `ERROR` explícito y se desconecta, en lugar de esperar silenciosamente hasta un timeout de registro; esto no elude el bootstrap ni cambia la selección de autoridad.

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

Consultar sólo un bloque devuelve metadatos (registros, tamaño, mtime, digest SHA-256 y estado de sincronización). Una ruta devuelve su valor o sus hijos inmediatos.

#### Advertencia de redacción de secretos

El código actual de `udb_query_is_secret()` oculta explícitamente:

- `N::*::pass`;
- `S::encryption_key`;
- el valor completo de `C::<canal>::modes`, porque puede contener una clave nativa `+k`.

Los avisos de debug de modos sustituyen igualmente todos los parámetros por `<redacted>` cuando la expresión de modos contiene `k`.


## 15. Seguridad e invariantes

La implementación aplica varias reglas de fail-closed:

- no publica candidatos de arranque parcialmente válidos;
- no acepta snapshots de una fuente que no sea la autoridad de esa ronda;
- no aplica un snapshot staged antes de validar el digest canónico SHA-256 y la persistencia;
- las mutaciones se validan contra el esquema antes de commit;
- las escrituras de fichero usan temporales exclusivos y permisos `0600`;
- no sigue symlinks en temporales cuando `O_NOFOLLOW` está disponible;
- no borra un temporal si resulta ser symlink o fichero no regular durante la limpieza defensiva;
- un error de durabilidad después de rename retira READY;
- HEL/OCL incompatibles pueden cerrar el enlace, evitando una red mixta silenciosamente inconsistente.

### 15.1 Frontera de confianza y transporte

HEL 4 no autentica al servidor ni al payload. UDB confía en el enlace de servidores autenticado de UnrealIRCd y después aplica su propia política de autoridad por peer directo, secuenciación, staging y esquema. Cada salto UDB debe usar por tanto TLS con verificación de certificado y credenciales de enlace estrictamente controladas. Sin TLS, el protocolo DB expone snapshots completos y valores en vivo durante el tránsito, incluidos hashes de contraseña, material de cifrado y claves de canal; la integridad también depende de la autenticación del enlace subyacente. UDB no aporta cifrado ni firma end-to-end por encima del enlace de servidores.

No edites archivos binarios nativos con el daemon activo. Los cambios manuales eluden la propiedad de generaciones inmutables y pueden corromper páginas, schemas, digests o la autoridad global WAL/checkpoints.

## 16. Rehash y cambios de política

En rehash:

- la configuración nueva no borra prematuramente el propagador activo si el rehash falla;
- al completar con éxito, si desapareció `udb::propagator`, se elimina el override local y vuelve a aplicarse la precedencia normal;
- se notifica el cambio de política y se cancelan sesiones que ya no pertenezcan a la fuente correcta;
- los titulares identificados conservan nickname, cuenta y efectos: los marcadores de identidad aplicada se preservan durante la recarga por rehash y se reconcilia contra los registros recargados, y el estado preservado se revoca si la base recargada no queda READY o el módulo no consigue recargar;
- se reconstruye el inventario OCL local y, si cambia, se publica una nueva generación.

Cambios de `S::propagator` tienen el mismo efecto de re-evaluación en runtime después de que el nuevo bloque/registro haya sido comprometido.

## 17. Build y bundle

Fuentes canónicas: `src/`. Artefacto de distribución: `dist/udb.c`.

```bash
scripts/format-sources.sh
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

INF <round> <block> <sha256> <record_count> 0 <watermark_seq> <root-SID> <root-epoch>
RES <round> <block>
BEGIN <round> <block> <txid> <sha256> <watermark_seq>
PUT <round> <block> <txid> <Block::path> :<typed-value>
END <round> <block> <txid> <sha256> <watermark_seq>
ACK <round> <block> <txid> <sha256> <watermark_seq>
ERR <subcmd> <code> <round/correlation> <block>

INS <epoch> <seq> <Block::path> :<typed-value>
DEL <epoch> <seq> <Block::path>
DRP <epoch> <seq> <block>
EXP <path> <expected-expires>

MANIFEST REQ <round>
MANIFEST ACK <round> <block> <count> <sha256> <watermark_seq>

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
4. usar `/DBQ N`, `/DBQ C`, etc. para comparar digests SHA-256 y metadatos;
5. usar `/UDB OPERCLASSES` para detectar inventarios OCL ausentes;
6. usar `/UDB OPERCLASS <name>` para detectar diferencias de definición/herencia/ACL;
7. revisar eventos `udb` de UnrealIRCd, especialmente HEL, persistencia, staged sync, READY y OCL;
8. conservar el almacén binario completo detenido y los logs para diagnóstico; no editar datos activos.

## 20. Limitaciones actuales verificadas

A fecha del commit documentado:

- el valor de `C::<canal>::access::<nick>` no define rango; la presencia de la entrada + identificación `+r` es lo que autoriza el JOIN.
- `C::<canal>::modes` rechaza el marcador de registro `r`, los rangos de miembro (`q`, `a`, `o`, `h`, `v`), `P` y `O`; el `+r` nativo sigue el estado de registro y de `suspend`, mientras que `P` y `O` proceden sólo de `C::options`.
- las claves raíz del bloque I se buscan de forma exacta; no son reglas CIDR de matching.
- `PERSISTENT` depende de que exista el modo nativo de canal `+P` en UnrealIRCd; UDB no crea un sustituto.
- `OPER_ONLY` aplica el JOIN desde el estado UDB incluso cuando no está disponible el handler nativo `+O` de UnrealIRCd.

Estas observaciones describen el comportamiento real del código y son deliberadamente explícitas para evitar documentar capacidades que aún no están conectadas a runtime.

## 21. Evidencia de validación en Linux

Registrado en Linux x86-64, 2026-10-10 UTC. El [informe JSON](native_storage_benchmarks.json) incluye nueve ejecuciones de una muestra y la prueba separada de escritura puntual.

| Filas | Caché MiB | Recuperación (s) | 10K lecturas repetidas (ms) | Pico RSS (MiB) |
|---:|---:|---:|---:|---:|
| 1,000,000 | 128 | 2.400 | 1055 | 141.9 |
| 1,000,000 | 256 | 2.415 | 845 | 273.9 |
| 1,000,000 | 512 | 2.315 | 456 | 537.9 |
| 5,000,000 | 128 | 6.398 | 1084 | 142.2 |
| 5,000,000 | 256 | 5.900 | 878 | 273.9 |
| 5,000,000 | 512 | 5.751 | 557 | 538.1 |
| 10,000,000 | 128 | 12.597 | 1069 | 141.8 |
| 10,000,000 | 256 | 10.443 | 879 | 273.9 |
| 10,000,000 | 512 | 12.059 | 715 | 538.0 |

Las filas son propiedades vhost de nicks distintos, no perfiles completos. La
creación masiva usa el sorter/rebuilder real con el writer detenido y serializado
por su propietario; no es una importación de 10M por protocolo. La recuperación
realiza toda la validación de datos referenciados. La primera lectura parte con
DATA fría, índices calientes y caché del sistema operativo conservada; la segunda
repite las mismas 10K claves. No son latencias de disco físicamente frío ni una
comparación estadísticamente controlada de tamaños de caché.

Las nueve ejecuciones masivas/lecturas alcanzaron 2,23–2,35 MiB de memoria AUX
contabilizada. Los ficheros estables ocuparon 1,74/1,99/2,45 GiB para 1M/5M/10M:
incluyen metadatos, páginas físicas COW/catálogo y extensiones muertas reutilizables,
no solo filas vivas. La prueba separada 10M/256 MiB ejecutó diez mutaciones puntuales
reales encoladas y durables más el enlace de origen en 59 ms, escribió 4.653.056
bytes mediante la E/S posicionada real y no amplió los ficheros gracias a la
reutilización de extensiones. Su pico AUX fue 2,89 MiB. Comprueba que el snapshot
anterior retenido todavía devuelve el valor original; no mide escritura sostenida
ni demuestra que todas las actualizaciones tengan ese coste.

La validación del contrato nuevo cubre valores tipados, corrupción, colas WAL
incompletas, EIO/ENOSPC, salida del proceso antes/durante WAL y tras su flush,
checkpoint con pins, división/colapso radix, límites de caché/cola, importación
atómica de seis bloques, rechazo de checksum, replay retenido, publicación de
efectos nativos, reinicio, rehash local, failover/failback ordenado y snapshots A→B→C/mutaciones multihop con
origen raíz. Los inventarios OCL se validan aparte; no es prueba exhaustiva de
OCL multihop ni de todas las topologías de failover.

Los límites de caché/AUX acotan las asignaciones gestionadas de almacenamiento,
no la memoria total del daemon. TKL/spamfilters nativos, canales persistentes,
usuarios conectados y propiedad de efectos se asignan aparte en UnrealIRCd: millones
de sanciones K activas o canales C persistentes pueden consumir memoria adicional.
El RSS registrado es del driver con propiedades N vhost, no de esa carga del daemon.

Siguen sin verificarse la ejecución real MSVC v142/Windows 11 o Server 2022/2025
con NTFS local, sus ACL/crashes, pérdida de alimentación del host y carga sostenida
de importación por red/escrituras. El éxito Linux no demuestra esas garantías.
