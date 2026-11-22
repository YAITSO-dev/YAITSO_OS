#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$DIST_DIR/repo"

[ -n "${HOST:-}" ] || { echo "repo-sync: HOST не задан" >&2; exit 1; }
DEST="${DEST:-/var/www/yaitso-repo/}"
DRY="${DRY:-0}"

RSYNC_OPTS="-avz --delete --exclude=key.sec --exclude=*.tmp"
[ "$DRY" = "1" ] && RSYNC_OPTS="$RSYNC_OPTS --dry-run"

echo ">>> sync $REPO -> $HOST:$DEST"
# shellcheck disable=SC2086
rsync $RSYNC_OPTS "$REPO/" "$HOST:$DEST"

echo ">>> готово"
