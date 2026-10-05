#!/usr/bin/env bash
# Manage the gateway on the VPS from this Mac.
# Usage: scripts/deploy.sh [deploy|status|logs|rollback|stop]
set -euo pipefail

HOST="${GATEWAY_HOST:-orbita-vps}"
DIR="${GATEWAY_DIR:-/var/www/stackchan-gateway}"
cd "$(dirname "$0")/.."

case "${1:-deploy}" in
deploy)
  npm run check
  npm test
  ssh "$HOST" "mkdir -p '$DIR/data/historias'"
  # .env and the story library are never overwritten or deleted by a deploy.
  rsync -az --delete --exclude /node_modules/ --exclude /.env --exclude /data/ ./ "$HOST:$DIR/"
  rsync -az data/ "$HOST:$DIR/data/"
  ssh "$HOST" "set -e; cd '$DIR'
    [ -f .env ] || { cp .env.example .env; chmod 600 .env; }
    if docker image inspect stackchan-gateway:latest >/dev/null 2>&1; then
      docker tag stackchan-gateway:latest stackchan-gateway:previous
    fi
    docker compose build --pull
    docker compose up -d
    sleep 5
    docker compose ps"
  ;;
status)
  ssh "$HOST" "cd '$DIR' && docker compose ps && docker exec stackchan-gateway cat /tmp/gateway-status.json && echo"
  ;;
logs)
  ssh "$HOST" "cd '$DIR' && docker compose logs --no-log-prefix --tail='${LINES:-100}'"
  ;;
rollback)
  ssh "$HOST" "set -e; cd '$DIR'
    docker image inspect stackchan-gateway:previous >/dev/null
    docker tag stackchan-gateway:previous stackchan-gateway:latest
    docker compose up -d --no-build --force-recreate
    docker compose ps"
  ;;
stop)
  ssh "$HOST" "cd '$DIR' && docker compose down"
  ;;
*)
  echo "usage: $0 [deploy|status|logs|rollback|stop]" >&2
  exit 2
  ;;
esac
