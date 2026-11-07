#!/bin/sh
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
YGLIBC="$DIST_DIR/tools/yglibc"

[ -x "$YGLIBC" ] || { echo "нет yglibc, соберите: make tools"; exit 1; }

echo ">>> yglibc info"
"$YGLIBC" info || true

if [ ! -e /usr/glibc/lib/ld-linux-x86-64.so.2 ]; then
    echo ">>> glibc не установлена, пропуск runtime-тестов"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/hello.c" <<EOF
#include <stdio.h>
int main(void) { printf("hello from glibc\n"); return 0; }
EOF

if command -v gcc >/dev/null 2>&1; then
    gcc -o "$TMP/hello" "$TMP/hello.c"
    "$YGLIBC" verify "$TMP/hello" || true
    "$YGLIBC" run "$TMP/hello"
fi

echo ">>> glibc.sh OK"
