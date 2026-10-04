# Suite de tests de reemplazo

[English](README.md)

La suite con cmocka + pytest es la suite de tests autoritativa. Sus aserciones
se derivan del código fuente actual de UDB, el protocolo y el comportamiento
documentado; las expectativas anteriores y los recuentos de checkpoints AST
no son requisitos de comportamiento ni métricas de finalización. Los scripts
de test independientes anteriores a pytest no forman parte de la suite ni son
puntos de entrada del CI.

## Preparación local

Requisitos: Python 3.12+, CMake, compilador C, bibliotecas de desarrollo
OpenSSL/PCRE2 y fuentes configuradas de UnrealIRCd 6.2.x. Los casos runtime
necesitan además una instalación de UnrealIRCd para tests y bubblewrap. La
ausencia de capacidades obligatorias produce un fallo, no un skip silencioso.

Desde la raíz del repositorio:

```bash
python3 -m venv tests/.build/venv
. tests/.build/venv/bin/activate
python3 -m pip install -r tests/requirements.txt
python3 tests/support/bootstrap_cmocka.py
export UNREALIRCD_SOURCE=/path/to/configured/unrealircd-source
```

El bootstrap explícito descarga cmocka 2.0.2 con checksum fijado y lo instala
sólo bajo `tests/.build/`, ignorado por Git. La colección no instala dependencias.
`UNREALIRCD_SOURCE` permite indicar la ubicación de las fuentes configuradas;
`CMOCKA_PREFIX` permite indicar otra instalación privada de cmocka.

## Ejecutar la familia relevante más pequeña

```bash
python3 -m pytest -q -m tooling
python3 -m pytest -q -m unit
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_odr_violation=2 \
  python3 -m pytest -q -m unit --c-profile asan
```

Cada caso C canónico incluye `src/udb.c`, se ejecuta en su propio proceso cmocka
y utiliza adaptadores estrictos sólo para dependencias externas del daemon.
Los helpers de UDB no se sustituyen por mocks. `--c-profile asan` instrumenta
estos ejecutables C con ASan/UBSan; no recompila ni instrumenta el daemon instalado.

Antes de los tests runtime, compila el módulo canónico dentro de las fuentes
configuradas de UnrealIRCd (donde este checkout es `src/modules/third/udb`):

```bash
make -C "$UNREALIRCD_SOURCE" custommodule MODULEFILE=udb/src/udb
export UDB_MODULE_PATH="$PWD/src/udb.so"
# Indicarlo si la instalación de tests no está en $HOME/unrealircd:
export UDB_TEST_IRCD_ROOT=/path/to/installed/test-runtime
python3 -m pytest -q -m 'integration or protocol or recovery or model'
```

Cada fixture copia el módulo indicado en nodos loopback desechables; los mounts
de sólo lectura protegen la instalación y los grupos de procesos propios
permiten su limpieza. Los tests no reemplazan el módulo instalado del usuario
ni reinician servidores existentes. Los tests runtime con sanitizers requieren
un daemon y módulo compilados por separado con sanitizers, como proporciona
la matriz del CI.

## Descubrimiento completo e informes

```bash
python3 -m pytest -q --junitxml tests/.build/results.xml
```

El CI ejecuta gates actuales de C unit/tooling y runtime/protocolo/recuperación/modelo
en los perfiles normal y sanitizer existentes. Los casos C canónicos activan la detección de fugas y ODR.
Los casos runtime conservan el entorno sanitizer existente del daemon; esto no
significa que tengan activada la detección de fugas del daemon. El benchmark
offline del índice hash es opt-in: ejecutar `python3 -m pytest -q -m perf tests/perf`
o añadir `--run-perf` a una ejecución más amplia. Sólo mide el parseo y la
distribución hash, no el rendimiento runtime del daemon ni fsync.

Los tests nuevos comprueban comportamiento y digests calculados de forma
independiente, no el texto de las fuentes C de producción. Una entrada inválida,
timeout, divergencia o infraestructura obligatoria ausente no debe convertirse
en `xfail`, reintento ni skip por capacidades.
