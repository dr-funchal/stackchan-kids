#!/usr/bin/env bash
# One-time setup of https://m5.pulpfy.com on the VPS (shared with other production sites).
# Adds two new files, validates with `nginx -t` BEFORE reloading, and removes them again if validation fails,
# so the other sites are never left with a broken config. Then issues the TLS certificate with certbot.
# Needs the DNS record first: A  m5.pulpfy.com -> the VPS public IP
set -euo pipefail

HOST="${GATEWAY_HOST:-orbita-vps}"
cd "$(dirname "$0")/.."

scp -q deploy/nginx/m5.pulpfy.com "$HOST:/tmp/m5.pulpfy.com"
scp -q deploy/nginx/m5-pulpfy.conf "$HOST:/tmp/m5-pulpfy.conf"

ssh "$HOST" bash -s <<'REMOTE'
set -euo pipefail
SITE=/etc/nginx/sites-available/m5.pulpfy.com
LINK=/etc/nginx/sites-enabled/m5.pulpfy.com
CONF=/etc/nginx/conf.d/m5-pulpfy.conf

if [ ! -e "$SITE" ]; then
  install -m 644 /tmp/m5.pulpfy.com "$SITE"
  install -m 644 /tmp/m5-pulpfy.conf "$CONF"
  ln -s "$SITE" "$LINK"
  if ! nginx -t; then
    rm -f "$LINK" "$SITE" "$CONF"
    echo "nginx -t falhou: arquivos removidos, nginx NÃO foi recarregado." >&2
    exit 1
  fi
  systemctl reload nginx
  echo "Site m5.pulpfy.com adicionado ao nginx."
else
  echo "Site m5.pulpfy.com já existe no nginx; mantendo como está."
fi
rm -f /tmp/m5.pulpfy.com /tmp/m5-pulpfy.conf

resolved=$(getent ahostsv4 m5.pulpfy.com | awk 'NR==1{print $1}')
if [ -z "$resolved" ] || ! hostname -I | tr ' ' '\n' | grep -qx "$resolved"; then
  echo "DNS ainda não aponta m5.pulpfy.com para esta VPS (resolve para: '${resolved:-nada}')." >&2
  echo "Crie o registro A na Hostinger e rode este script de novo para emitir o certificado." >&2
  exit 1
fi

if certbot certificates 2>/dev/null | grep -q "Certificate Name: m5.pulpfy.com"; then
  echo "Certificado já existe."
else
  certbot --nginx -d m5.pulpfy.com --non-interactive --redirect
fi
curl -fsS -m 10 https://m5.pulpfy.com/health && echo "  <- https://m5.pulpfy.com/health OK"
REMOTE
