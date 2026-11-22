#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$DIST_DIR/repo"
OUT="$REPO/index.txt"

[ -d "$REPO" ] || { echo "build-repo: нет $REPO" >&2; exit 1; }

echo ">>> индексация пакетов"
: > "$OUT"

for f in "$REPO"/*.yat; do
    [ -f "$f" ] || continue
    base=$(basename "$f" .yat)
    echo "$base" >> "$OUT"
done

sort -o "$OUT" "$OUT"

echo ">>> index.txt: $(wc -l < "$OUT") пакетов"
