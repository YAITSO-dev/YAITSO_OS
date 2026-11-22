#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$DIST_DIR/repo"
KEY="${KEY:-$DIST_DIR/repo/key.sec}"

[ -f "$REPO/index.txt" ] || { echo "repo-sign: нет index.txt" >&2; exit 1; }

if [ ! -f "$KEY" ]; then
    echo ">>> генерация ключа $KEY"
    "$DIST_DIR/yat/target/release/yat-keygen" -s "$KEY" -p "$REPO/key.pub"
fi

echo ">>> подпись index.txt"
"$DIST_DIR/yat/target/release/yat-sign" -k "$KEY" "$REPO/index.txt" "$REPO/index.sig"

echo ">>> index.sig создан"
ls -l "$REPO/index.sig"
