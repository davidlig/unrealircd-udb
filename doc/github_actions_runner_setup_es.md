# Guía local: configurar runner, despliegue y TLS desde cero

Esta guía contiene comandos copiables para reconstruir la automatización. **Debe permanecer sin versionar**. Sustituye exclusivamente los marcadores `<...>` o las variables de la primera sección; no añadas valores reales de host, usuario, DNS, correo, token, rutas privadas ni secretos al fichero.

> La única acción que requiere administrador es habilitar `linger` una vez. El runner, Certbot, systemd de usuario y el despliegue diario funcionan sin `sudo`.

## 0. Definir valores locales

En una terminal de la cuenta de despliegue, crea un fichero privado de variables. No lo copies al repositorio:

```bash
install -d -m 0700 "$HOME/.local/share/ircd-actions"
cat > "$HOME/.local/share/ircd-actions/setup.env" <<'ENV'
export DEPLOY_USER='<deployment-user>'
export DEPLOY_UID='<numeric-user-id>'
export DEPLOY_GID='<numeric-group-id>'
export INSTALL_ROOT='<private-install-root>'
export EXISTING_IRCD_ROOT='<existing-ircd-root>'
export EXISTING_SOURCE_ROOT='<existing-unrealircd-source-root>'
export ACTIONS_ROOT="$HOME/.local/share/ircd-actions"
export RUNNER_STATE="$ACTIONS_ROOT/runner-state"
export RUNNER_COMMAND_DIR="$ACTIONS_ROOT/runner-command"
export CERTBOT_STATE="$ACTIONS_ROOT/certbot"
export RUNNER_URL='https://github.com/<owner>/<repository>'
export RUNNER_NAME='ircd-deploy'
export RUNNER_LABELS='ircd-deploy'
export RUNNER_REGISTRATION_TOKEN='<single-use-registration-token>'
export TLS_NAME='<public-tls-name>'
export CERTBOT_EMAIL='<certificate-contact-email>'
ENV
chmod 600 "$HOME/.local/share/ircd-actions/setup.env"
# Sustituye los marcadores y vuelve a cargar las variables:
. "$HOME/.local/share/ircd-actions/setup.env"
```

Obtén los valores numéricos sin anotar su resultado en esta guía:

```bash
id -u
id -g
```

Un administrador debe ejecutar una sola vez, sustituyendo el usuario:

```bash
sudo loginctl enable-linger '<deployment-user>'
```

La cuenta de despliegue puede confirmar el resultado:

```bash
loginctl show-user "$DEPLOY_USER" -p Linger
# Debe mostrar: Linger=yes
```

## 1. Crear directorios privados

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"

install -d -m 0700 \
  "$ACTIONS_ROOT" \
  "$RUNNER_STATE" \
  "$RUNNER_COMMAND_DIR" \
  "$CERTBOT_STATE/etc" \
  "$CERTBOT_STATE/lib" \
  "$CERTBOT_STATE/log" \
  "$INSTALL_ROOT"

# El árbol de ejecución será persistente; src se recreará en cada despliegue.
install -d -m 0700 "$INSTALL_ROOT/ircd" "$INSTALL_ROOT/src"
```

## 2. Crear el Compose privado

Guarda este Compose **fuera** del repositorio. Fija las imágenes oficiales por digest antes de usar producción:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"

cat > "$ACTIONS_ROOT/compose.yaml" <<'YAML'
services:
  runner:
    image: ghcr.io/actions/actions-runner@<reviewed-image-digest>
    init: true
    restart: unless-stopped
    user: "${DEPLOY_UID}:${DEPLOY_GID}"
    environment:
      RUNNER_URL: ${RUNNER_URL:-}
      RUNNER_TOKEN: ${RUNNER_REGISTRATION_TOKEN:-}
      RUNNER_NAME: ${RUNNER_NAME:-ircd-deploy}
      RUNNER_LABELS: ${RUNNER_LABELS:-ircd-deploy}
      HOME: /runner-state
    volumes:
      - ${RUNNER_STATE}:/runner-state
      - ${RUNNER_COMMAND_DIR}:/runner-command:ro
    working_dir: /runner-state
    entrypoint: ["/bin/bash"]
    command:
      - -lc
      - |
        set -euo pipefail
        if [[ ! -x ./run.sh ]]; then
          cp -R /home/runner/. /runner-state/
          chmod -R u+rwX,go-rwx /runner-state
        fi
        if [[ ! -f ./.runner ]]; then
          : "$${RUNNER_URL:?RUNNER_URL is required for initial registration}"
          : "$${RUNNER_TOKEN:?RUNNER_TOKEN is required for initial registration}"
          ./config.sh --unattended --replace --url "$$RUNNER_URL" --token "$$RUNNER_TOKEN" --name "$$RUNNER_NAME" --labels "$$RUNNER_LABELS" --work "_work"
        fi
        exec ./run.sh

  certbot:
    image: certbot/certbot@<reviewed-image-digest>
    profiles: ["certbot"]
    user: "${DEPLOY_UID}:${DEPLOY_GID}"
    ports:
      - "80:80"
    volumes:
      - ${CERTBOT_STATE}/etc:/etc/letsencrypt
      - ${CERTBOT_STATE}/lib:/var/lib/letsencrypt
      - ${CERTBOT_STATE}/log:/var/log/letsencrypt
YAML

cd "$ACTIONS_ROOT"
docker compose config >/dev/null
```

El Compose no contiene ni debe recibir `docker.sock`. Compruébalo antes del arranque:

```bash
! grep -q '/var/run/docker.sock' "$ACTIONS_ROOT/compose.yaml"
```

## 3. Registrar y comprobar el runner

Genera `RUNNER_REGISTRATION_TOKEN` desde **GitHub → Settings → Actions → Runners → New self-hosted runner**. Es de un solo uso.

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
cd "$ACTIONS_ROOT"
docker compose up -d runner
docker compose ps
docker compose logs --tail=100 runner
```

Cuando GitHub muestre el runner como *Online*, borra el token del fichero de variables y reinicia el contenedor para que no permanezca en su entorno:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
sed -i "s|^export RUNNER_REGISTRATION_TOKEN=.*|export RUNNER_REGISTRATION_TOKEN=''|" "$ACTIONS_ROOT/setup.env"
cd "$ACTIONS_ROOT"
docker compose up -d --force-recreate runner

docker inspect "$(docker compose ps -q runner)" \
  --format '{{range .Mounts}}{{println .Destination}}{{end}}' \
  | grep -qx '/runner-command'
! docker inspect "$(docker compose ps -q runner)" \
  --format '{{range .Mounts}}{{println .Destination}}{{end}}' \
  | grep -q '/var/run/docker.sock'
```

## 4. Crear el transporte local por socket Unix

No se abre ningún puerto: el runner usa el socket Unix privado montado en `/runner-state/deploy.sock`. systemd de usuario activa un proceso por solicitud y ese proceso invoca el ejecutor fijo del host.

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
cat > "$RUNNER_COMMAND_DIR/submit-deploy" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
(( $# == 6 )) || { echo 'expected six deployment arguments' >&2; exit 64; }
exec python3 - "$@" <<'PY2'
import json, socket, sys
marker=b'__UDB_DEPLOY_STATUS__:'
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as c:
    c.settimeout(35 * 60); c.connect('/runner-state/deploy.sock')
    c.sendall(json.dumps(sys.argv[1:], separators=(',', ':')).encode()+b'\n'); c.shutdown(socket.SHUT_WR)
    data=b''.join(iter(lambda: c.recv(65536), b''))
pos=data.rfind(marker)
if pos < 0: sys.stderr.buffer.write(data); raise SystemExit('deployment service returned no status')
sys.stdout.buffer.write(data[:pos]); raise SystemExit(int(data[pos+len(marker):].strip().splitlines()[0]))
PY2
SCRIPT
chmod 700 "$RUNNER_COMMAND_DIR/submit-deploy"

cat > "$ACTIONS_ROOT/deploy-socket-server" <<EOF2
#!/usr/bin/env python3
import json, subprocess, sys
marker='__UDB_DEPLOY_STATUS__:'
executor='$ACTIONS_ROOT/deploy-executor'
raw=sys.stdin.buffer.readline(4097)
try: args=json.loads(raw.decode())
except Exception: args=None
if not isinstance(args, list) or len(args)!=6 or not all(isinstance(x,str) for x in args):
    print('deployment rejected: invalid socket request', file=sys.stderr); print(marker+'64'); raise SystemExit(0)
result=subprocess.run([executor,*args], check=False)
print(marker+str(result.returncode), flush=True)
EOF2
chmod 700 "$ACTIONS_ROOT/deploy-socket-server"

cat > "$HOME/.config/systemd/user/ircd-deploy.socket" <<EOF2
[Socket]
ListenStream=$RUNNER_STATE/deploy.sock
SocketMode=0600
Accept=yes
RemoveOnStop=true
[Install]
WantedBy=sockets.target
EOF2
cat > "$HOME/.config/systemd/user/ircd-deploy@.service" <<EOF2
[Service]
Type=exec
ExecStart=$ACTIONS_ROOT/deploy-socket-server
StandardInput=socket
StandardOutput=socket
StandardError=socket
TimeoutStartSec=35min
KillMode=mixed
UMask=0077
NoNewPrivileges=yes
EOF2
systemctl --user daemon-reload
systemctl --user enable --now ircd-deploy.socket
systemctl --user is-active --quiet ircd-deploy.socket
```

Prueba solo el rechazo seguro antes de artefactos reales:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
cd "$ACTIONS_ROOT"; runner_id=$(docker compose ps -q runner)
set +e
docker exec "$runner_id" /runner-command/submit-deploy
status=$?
set -e
test "$status" -eq 64
```

## 5. Crear el ejecutor privado de despliegue

Inicializa una sola vez el árbol persistente a partir del servicio existente. Las rutas reales permanecen únicamente en `setup.env`:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
test -x "$EXISTING_IRCD_ROOT/bin/unrealircd"
test -f "$EXISTING_SOURCE_ROOT/config.settings"
cp -a -- "$EXISTING_IRCD_ROOT/." "$INSTALL_ROOT/ircd/"
install -m 0600 "$EXISTING_SOURCE_ROOT/config.settings" "$INSTALL_ROOT/src/config.settings"
```

Crea fuera del repositorio el ejecutor fijo que valida los artefactos, compila en un árbol temporal, conserva configuración y datos, y revierte si falla la activación:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"

cat > "$ACTIONS_ROOT/deploy-executor" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

readonly ENV_FILE="$HOME/.local/share/ircd-actions/setup.env"
[[ -f "$ENV_FILE" ]] || { echo 'deployment environment is missing' >&2; exit 1; }
# shellcheck disable=SC1090
. "$ENV_FILE"

readonly DEPLOY_ROOT="${INSTALL_ROOT:?}"
readonly TARGET="$DEPLOY_ROOT/ircd"
readonly SOURCE="$DEPLOY_ROOT/src"
readonly RUNNER_STATE="${RUNNER_STATE:?}"
readonly SERVICE="unrealircd.service"
readonly LOCK_FILE="${ACTIONS_ROOT:?}/deploy.lock"

fail() {
  printf 'deployment rejected: %s\n' "$*" >&2
  exit 1
}

(( EUID != 0 )) || fail "must run as the deployment user"
(( $# == 6 )) || fail "expected exactly six arguments"

version=$1
unreal_container_path=$2
unreal_sha256=$3
udb_container_path=$4
udb_sha256=$5
commit=$6

[[ "$version" =~ ^6\.2\.[0-9]+$ ]] || fail "unsupported release version"
[[ "$unreal_sha256" =~ ^[0-9a-f]{64}$ ]] || fail "invalid UnrealIRCd checksum"
[[ "$udb_sha256" =~ ^[0-9a-f]{64}$ ]] || fail "invalid UDB checksum"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || fail "invalid commit"

map_runner_file() {
  local container_path=$1 candidate resolved
  case "$container_path" in
    /runner-state/_work/*) candidate="$RUNNER_STATE/${container_path#/runner-state/}" ;;
    *) fail "artifact is outside the runner work directory" ;;
  esac
  [[ -f "$candidate" && ! -L "$candidate" ]] || fail "artifact is not a regular non-symlink file"
  resolved=$(realpath -e -- "$candidate") || fail "artifact cannot be resolved"
  case "$resolved" in
    "$RUNNER_STATE"/_work/*) ;;
    *) fail "artifact escapes the runner work directory" ;;
  esac
  printf '%s\n' "$resolved"
}

unreal_archive=$(map_runner_file "$unreal_container_path")
udb_archive=$(map_runner_file "$udb_container_path")
printf '%s  %s\n' "$unreal_sha256" "$unreal_archive" | sha256sum --check --status - || fail "UnrealIRCd checksum mismatch"
printf '%s  %s\n' "$udb_sha256" "$udb_archive" | sha256sum --check --status - || fail "UDB checksum mismatch"

exec 9>"$LOCK_FILE"
flock -n 9 || fail "another deployment is running"
[[ -x "$TARGET/bin/unrealircd" ]] || fail "current target is not executable"
[[ -d "$TARGET/conf" ]] || fail "current configuration is missing"

work=$(mktemp -d "$DEPLOY_ROOT/.deploy.XXXXXX")
rollback_needed=0
rollback_dir="$work/rollback-ircd"

rollback() {
  local status=$?
  trap - EXIT
  if (( rollback_needed )); then
    systemctl --user stop "$SERVICE" || true
    if [[ -d "$rollback_dir" ]]; then
      rm -rf -- "$TARGET"
      mv -- "$rollback_dir" "$TARGET"
      systemctl --user start "$SERVICE" || true
    fi
  fi
  rm -rf -- "$work"
  exit "$status"
}
trap rollback EXIT

new_source="$work/src"
mkdir -p "$new_source"
tar -tzf "$unreal_archive" | awk '
  /^\// || /(^|\/)\.\.($|\/)/ { exit 1 }
  NF { seen=1 }
  END { exit seen ? 0 : 1 }
' || fail "unsafe or empty UnrealIRCd archive"
tar -xzf "$unreal_archive" --strip-components=1 --no-same-owner --no-same-permissions -C "$new_source"
[[ -x "$new_source/Config" ]] || fail "UnrealIRCd archive does not contain Config"
[[ -f "$SOURCE/config.settings" ]] || fail "current UnrealIRCd configuration settings are missing"
cp -a -- "$SOURCE/config.settings" "$new_source/config.settings"

mkdir -p "$new_source/src/modules/third/udb"
tar -tzf "$udb_archive" | awk '
  /^\// || /(^|\/)\.\.($|\/)/ { exit 1 }
  NF { seen=1 }
  END { exit seen ? 0 : 1 }
' || fail "unsafe or empty UDB archive"
tar -xzf "$udb_archive" --strip-components=1 --no-same-owner --no-same-permissions -C "$new_source/src/modules/third/udb"
[[ -f "$new_source/src/modules/third/udb/src/udb.c" ]] || fail "UDB archive layout is invalid"

(
  cd "$new_source"
  ./Config -quick --prefix="$TARGET"
  make -j"$(nproc)"
  make custommodule MODULEFILE=udb/src/udb
)
[[ -f "$new_source/src/modules/third/udb/src/udb.so" ]] || fail "UDB module was not produced"

cp -a -- "$TARGET" "$rollback_dir"
rollback_needed=1
systemctl --user stop "$SERVICE"
(
  cd "$new_source"
  make install
)

rm -rf -- "$TARGET/conf" "$TARGET/data"
cp -a -- "$rollback_dir/conf" "$TARGET/conf"
if [[ -d "$rollback_dir/data" ]]; then
  cp -a -- "$rollback_dir/data" "$TARGET/data"
fi
install -D -m 0755 "$new_source/src/modules/third/udb/src/udb.so" "$TARGET/modules/third/udb.so"
"$TARGET/unrealircd" configtest

rm -rf -- "$SOURCE"
mv -- "$new_source" "$SOURCE"
rm -f -- "$TARGET/source"
ln -s -- "$SOURCE" "$TARGET/source"
systemctl --user start "$SERVICE"
systemctl --user is-active --quiet "$SERVICE"
rollback_needed=0
rm -rf -- "$work"
trap - EXIT
printf 'deployed UnrealIRCd %s with UDB commit %s\n' "$version" "$commit"
SCRIPT

chmod 700 "$ACTIONS_ROOT/deploy-executor"
bash -n "$ACTIONS_ROOT/deploy-executor"
```

## 6. Instalar la unidad del IRCd

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"

cat > "$HOME/.config/systemd/user/unrealircd.service" <<EOF2
[Unit]
Description=UnrealIRCd deployed by the production runner
ConditionFileIsExecutable=$INSTALL_ROOT/ircd/bin/unrealircd

[Service]
Type=simple
WorkingDirectory=$INSTALL_ROOT/ircd
ExecStart=$INSTALL_ROOT/ircd/bin/unrealircd -F
ExecReload=$INSTALL_ROOT/ircd/unrealircd reloadtls
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF2

systemctl --user daemon-reload
systemctl --user enable unrealircd.service
# No iniciar hasta que exista una instalación válida y configtest haya pasado.
```

## 7. Emitir TLS y activar renovación

Antes de emitir, confirma que TCP/80 puede atender HTTP-01 y que ningún otro proceso ocupa el puerto.

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
cd "$ACTIONS_ROOT"

# Emisión inicial. El nombre y el correo proceden del fichero privado de variables.
docker compose --profile certbot run --rm --service-ports certbot certonly \
  --standalone --non-interactive --agree-tos \
  --email "$CERTBOT_EMAIL" \
  --domains "$TLS_NAME"
```

Crea enlaces estables **con marcadores sustituidos localmente** hacia el estado de Certbot y los directorios TLS usados por la configuración del IRCd:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
install -d -m 0700 "$INSTALL_ROOT/ircd/conf/tls"
ln -sfn "$CERTBOT_STATE/etc/live/$TLS_NAME/fullchain.pem" "$INSTALL_ROOT/ircd/conf/tls/client.crt"
ln -sfn "$CERTBOT_STATE/etc/live/$TLS_NAME/privkey.pem" "$INSTALL_ROOT/ircd/conf/tls/client.key"
```

Instala el renovador privado:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"

cat > "$ACTIONS_ROOT/certbot-renew" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
readonly ACTIONS_ROOT="$HOME/.local/share/ircd-actions"
cleanup() {
  cd "$ACTIONS_ROOT"
  docker compose --profile certbot rm -sf certbot >/dev/null 2>&1 || true
}
trap cleanup EXIT
cd "$ACTIONS_ROOT"
timeout 15m docker compose --profile certbot run --rm --service-ports certbot renew
systemctl --user is-active --quiet unrealircd.service && systemctl --user reload unrealircd.service
SCRIPT
chmod 700 "$ACTIONS_ROOT/certbot-renew"

cat > "$HOME/.config/systemd/user/ircd-certbot-renew.service" <<EOF2
[Unit]
Description=Renew the private IRCd client certificate

[Service]
Type=oneshot
ExecStart=$ACTIONS_ROOT/certbot-renew
EOF2

cat > "$HOME/.config/systemd/user/ircd-certbot-renew.timer" <<'EOF2'
[Unit]
Description=Run IRCd certificate renewal twice daily

[Timer]
OnCalendar=*-*-* 02,14:00:00
Persistent=true

[Install]
WantedBy=timers.target
EOF2

systemctl --user daemon-reload
systemctl --user enable --now ircd-certbot-renew.timer
systemctl --user list-timers ircd-certbot-renew.timer
```

Prueba la renovación en una ventana suficientemente larga. Si falla o se interrumpe, comprueba que no deja un contenedor ni listener TCP/80:

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
systemctl --user start --wait ircd-certbot-renew.service
! docker ps -a --format '{{.Names}}' | grep -q '^ircd-actions-certbot-run-'
! ss -ltn 'sport = :80' | grep -q LISTEN
```

## 8. Configurar GitHub y desplegar

1. En **Settings → Environments → production**, limita el despliegue a la rama de producción.
2. En **Environment secrets**, crea `DEPLOY_COMMAND` con la ruta visible en el contenedor:

   ```text
   /runner-command/submit-deploy
   ```

   No uses una variable de Environment ni una ruta del host.

3. Desde GitHub CLI, si la sesión ya está autenticada, el mismo secreto se puede crear así:

   ```bash
   printf '%s' '/runner-command/submit-deploy' \
     | gh secret set DEPLOY_COMMAND --env production --repo '<owner>/<repository>'
   ```

4. Publica el workflow solo después de sus pruebas y comprueba los runs:

   ```bash
   git status --short
   python3 .agentic/test_ci_workflow.py
   python3 tests/test_release_manifest.py
   python3 .agentic/generate.py --check
   ./.agentic/ci-check.sh
   git add .github/workflows/ci.yml .agentic/test_ci_workflow.py \
     scripts/resolve_unrealircd_release.py tests/test_release_manifest.py
   git commit -m 'ci: automate UnrealIRCd UDB deployment'
   git push
   gh run list --limit 5
   ```

## Lista de comprobación final

```bash
set -euo pipefail
. "$HOME/.local/share/ircd-actions/setup.env"
cd "$ACTIONS_ROOT"

docker compose ps
systemctl --user is-enabled --quiet unrealircd.service
systemctl --user is-active --quiet unrealircd.service
systemctl --user is-enabled --quiet ircd-certbot-renew.timer
loginctl show-user "$DEPLOY_USER" -p Linger
! docker inspect "$(docker compose ps -q runner)" \
  --format '{{range .Mounts}}{{println .Destination}}{{end}}' \
  | grep -q '/var/run/docker.sock'
```

La guía sigue siendo local: comprueba antes de cada commit que permanezca sin seguimiento:

```bash
git status --short -- doc/github_actions_runner_setup_es.md
# Debe mostrar: ?? doc/github_actions_runner_setup_es.md
```
