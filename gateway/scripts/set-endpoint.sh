#!/usr/bin/env bash
# Saves the xiaozhi.me MCP endpoint into the VPS .env and restarts the gateway.
# The URL carries a secret token: it is read hidden, sent over ssh stdin, and never shown or kept in shell history.
set -euo pipefail

HOST="${GATEWAY_HOST:-orbita-vps}"
DIR="${GATEWAY_DIR:-/var/www/stackchan-gateway}"

read -rsp "Cole o endereço MCP do app (wss://api.xiaozhi.me/mcp/?token=...). Ele não aparece na tela: " url
echo
case "$url" in
wss://*token=*) ;;
*)
  echo "Endereço inválido: precisa começar com wss:// e conter token=" >&2
  exit 1
  ;;
esac

printf '%s\n' "$url" | ssh "$HOST" "set -e; cd '$DIR'; umask 077
  IFS= read -r url
  [ -f .env ] || cp .env.example .env
  grep -v '^MCP_ENDPOINT=' .env > .env.tmp || true
  printf 'MCP_ENDPOINT=%s\n' \"\$url\" >> .env.tmp
  mv .env.tmp .env
  chmod 600 .env
  docker compose up -d --force-recreate
  echo 'Endereço salvo. Gateway reiniciado.'"
