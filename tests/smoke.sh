#!/bin/sh
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SHELL_BIN="$DIST_DIR/yaitso"

[ -x "$SHELL_BIN" ] || { echo "нет $SHELL_BIN"; exit 1; }

pass=0
fail=0

check() {
    desc="$1"
    expected="$2"
    actual="$3"
    if [ "$expected" = "$actual" ]; then
        pass=$((pass + 1))
        printf "  ok   %s\n" "$desc"
    else
        fail=$((fail + 1))
        printf "  FAIL %s (ожидалось '%s', получено '%s')\n" \
               "$desc" "$expected" "$actual"
    fi
}

echo ">>> smoke-тесты шелла"

out=$("$SHELL_BIN" -c 'echo hello')
check "echo hello" "hello" "$out"

out=$("$SHELL_BIN" -c 'echo -n world')
check "echo -n world" "world" "$out"

out=$("$SHELL_BIN" -c 'printf "%d-%s" 42 x')
check "printf" "42-x" "$out"

out=$("$SHELL_BIN" -c 'true; echo $?')
check "true exit" "0" "$out"

out=$("$SHELL_BIN" -c 'false; echo $?')
check "false exit" "1" "$out"

out=$("$SHELL_BIN" -c 'echo a; echo b')
expected="a
b"
check "seq ;" "$expected" "$out"

out=$("$SHELL_BIN" -c 'export FOO=bar; echo $FOO')
check "export" "bar" "$out"

out=$("$SHELL_BIN" -c 'seq 1 3')
expected="1
2
3"
check "seq" "$expected" "$out"

out=$("$SHELL_BIN" -c 'echo abc | grep b')
check "pipe grep" "abc" "$out"

out=$("$SHELL_BIN" -c 'true && echo yes')
check "&&" "yes" "$out"

out=$("$SHELL_BIN" -c 'false || echo fallback')
check "||" "fallback" "$out"

echo ""
echo "smoke: $pass ok, $fail fail"
[ "$fail" = "0" ] || exit 1
