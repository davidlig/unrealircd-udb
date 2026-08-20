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

### 1.2 Bloques Soportados y sus Opciones

#### Bloque N (Nicks - Usuarios)
Almacena configuraciones para usuarios registrados.
*   **pass**: Contraseña del usuario (texto plano o hash como `sha256:hash`).
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
*   **options**: Opciones numéricas del canal, incluido el candado de modos.

### 1.3 Reconciliación De Canales En Caliente

UDB controla el estado `+q` del fundador de un canal registrado. Al sustituir
`founder`, retira `+q` al fundador anterior presente y se lo concede al nuevo
fundador identificado. UDB nunca concede `+o` al fundador.

`pass` y `challenge` autentican a un usuario al entrar en ese canal y conceden
únicamente `+a` durante esa membresía. No conceden `+o`. Sustituir o borrar
cualquiera de esas credenciales revoca `+a` solo cuando UDB lo había concedido.
Borrar el perfil del canal revoca los privilegios de fundador y administrador
gestionados por UDB y limpia el topic persistente.

#### Bloque I (IPs y Hosts)
*   **clones**: Límite numérico de conexiones simultáneas (`*<numero>`).
*   **host**: Override de host aplicado antes de completar una conexión local.
*   **nolines**: Letras de exención de sanciones (ej. `GZT` para eximir de G-Lines, Z-Lines, etc.).

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
*   **clones**: Límite numérico global para IPs no especificadas en el bloque I.
*   **challenge**: Tipo de hash por defecto para contraseñas.
*   **quit_clones**: Mensaje de salida (quit message) para conexiones expulsadas por límite.
*   **bot_nick**: NickName virtual para mensajes del sistema UDB (ej. `UDB-Bot`).
*   **bot_mask**: Máscara virtual para el bot del sistema (ej. `servicios@red.com`).

#### Bloque L (Enlaces S2S)
*   **options**: Opciones numéricas mediante máscara de bits (`*1` habilita logs S2S de depuración).

---

## 2. Protocolo S2S (Server-to-Server)

El protocolo UDB se integra en el tráfico S2S nativo de UnrealIRCd utilizando el comando extendido `DB`.
Solo se sincronizan peers que anuncian la capacidad del módulo UDB. Las
mutaciones en caliente deben proceder del `udb::propagator` configurado; un
peer que sirve una sincronización activa solo puede enviar registros de ese
bloque.
**Estructura general:**
`:<sid_origen> DB <destino> <subcomando> <parametros>`

### 2.1 Sincronización Inicial (Handshake)
Cuando un servidor se conecta a otro, se verifica el estado de los bloques mediante CRC32.

**INF (Información del Bloque):**
`:<sid> DB <destino> INF <letra_bloque> <crc32_hex> <timestamp>`

**RES (Request / Petición de Sincronización):**
`:<sid> DB <destino> RES <letra_bloque>`

**FDR (Fin De Resumen):**
Marca el final de la transmisión masiva de un bloque.
`:<sid> DB <destino> FDR <letra_bloque>`

### 2.2 Modificación de Datos en Tiempo Real
Para inyectar o eliminar registros en caliente, se usan los siguientes comandos (generalmente con destino `*` para broadcast).

**INS (Insertar / Modificar):**
`:<sid> DB * INS <letra_bloque>::<clave>[::<subclave>] <valor>`
*Ejemplo:* `<sid> DB * INS N::davidlig::vhost admin.davidlig.net`

**DEL (Eliminar):**
Elimina un nodo en cascada.
`:<sid> DB * DEL <letra_bloque>::<clave>[::<subclave>]`
*Ejemplo:* `:<sid> DB * DEL C::#opers::topic`

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

---

## 3. Créditos y Licencia

**Autor Original (UnrealIRCd 3.x):**
El protocolo UDB y su versión original clásica fueron ideados y desarrollados por **Trocotronic** (*www.redyc.com*). El módulo actual y las optimizaciones del protocolo son desarrolladas y mantenidas bajo la URL del proyecto **https://github.com/davidlig/unrealircd-udb** por **David Abuín Fontán ('davidlig')**.

**Versión Actual (UnrealIRCd 6.2.x - UDB v4.0.0):**
Refactorización moderna, paso a la nueva arquitectura C modular y adaptación integral a la nueva API v6 de UnrealIRCd desarrollados por **David Abuín Fontán "davidlig"** (2026).
Se ha logrado convertir UDB en un módulo de terceros estándar y optimizado (`udb.so`), con integración nativa al motor de TKLs, seguridad criptográfica moderna y soporte total para el enrutamiento S2S v6.
