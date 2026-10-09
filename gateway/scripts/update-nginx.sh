#!/usr/bin/env bash
# Installs deploy/nginx/* for m5.pulpfy.com on the VPS. Touches only these two files; the nginx serves other
# production sites, so: backup first, `nginx -t` before reloading, and restore the backup if the test fails.
set -euo pipefail

HOST="${GATEWAY_HOST:-orbita-vps}"
DIR="${GATEWAY_DIR:-/var/www/stackchan-gateway}"
cd "$(dirname "$0")/.."

scp -q deploy/nginx/m5.pulpfy.com "$HOST:/tmp/m5.pulpfy.com.new"
scp -q deploy/nginx/m5-pulpfy.conf "$HOST:/tmp/m5-pulpfy.conf.new"

ssh "$HOST" "DIR='$DIR' bash -s" <<'REMOTE'
set -euo pipefail
SITE=/etc/nginx/sites-available/m5.pulpfy.com
CONF=/etc/nginx/conf.d/m5-pulpfy.conf
BACKUP="$DIR/backups/nginx-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
cp -p "$SITE" "$CONF" "$BACKUP/"
install -m 644 /tmp/m5.pulpfy.com.new "$SITE"
install -m 644 /tmp/m5-pulpfy.conf.new "$CONF"
rm -f /tmp/m5.pulpfy.com.new /tmp/m5-pulpfy.conf.new
if nginx -t 2>&1; then
  systemctl reload nginx
  echo "nginx atualizado (backup em $BACKUP)"
else
  cp -p "$BACKUP/m5.pulpfy.com" "$SITE"
  cp -p "$BACKUP/m5-pulpfy.conf" "$CONF"
  nginx -t && systemctl reload nginx
  echo "nginx -t falhou: configuração anterior restaurada" >&2
  exit 1
fi
REMOTE
