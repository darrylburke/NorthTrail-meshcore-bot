#!/bin/sh
# Installs (or updates) the extras program as a service that starts on boot.
# Run on the server, as root, from the mqtt-extras folder:   sudo sh deploy/install.sh
set -eu

APP=/opt/meshcore-bot-extras
ETC=/etc/meshcore-bot-extras
UNIT=meshcore-bot-extras.service
SRC=$(cd "$(dirname "$0")/.." && pwd)

[ "$(id -u)" = 0 ] || { echo "run this with sudo" >&2; exit 1; }
command -v node >/dev/null || { echo "node is not installed (version 18 or later is needed)" >&2; exit 1; }

install -d -m 755 "$APP" "$APP/vendor" "$APP/story"
install -m 644 "$SRC/extras.js" "$SRC/game.js" "$SRC/joke.js" "$SRC/package.json" "$SRC/package-lock.json" "$APP/"
install -m 644 "$SRC/vendor/jszm.js" "$APP/vendor/"
(cd "$APP" && npm ci --omit=dev --no-audit --no-fund --silent)
if [ ! -s "$APP/story/zork1.z3" ]; then
  if [ -s "$SRC/story/zork1.z3" ]; then cp "$SRC"/story/* "$APP/story/"; else (cd "$APP" && npm run --silent fetch-story); fi
fi
chmod -R a+rX "$APP"

install -d -m 700 "$ETC"
if [ ! -f "$ETC/mqtt.env" ]; then
  umask 077
  cat > "$ETC/mqtt.env" <<'ENV'
# Broker login for the extras program. Readable by root only.
MQTT_HOST=127.0.0.1
MQTT_PORT=1883
MQTT_USER=
MQTT_PASS=
MQTT_PREFIX=meshcore/MeshBot
ENV
  echo "Wrote $ETC/mqtt.env: fill in MQTT_USER, MQTT_PASS and MQTT_PREFIX, then run this script again."
  exit 0
fi
chmod 600 "$ETC/mqtt.env"

install -m 644 "$SRC/deploy/$UNIT" "/etc/systemd/system/$UNIT"
systemctl daemon-reload
systemctl enable "$UNIT" >/dev/null 2>&1
systemctl restart "$UNIT"
sleep 3
systemctl --no-pager --lines=8 status "$UNIT" || true
