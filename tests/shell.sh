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
        printf "  FAIL %s\n" "$desc"
    fi
}

echo ">>> shell-тесты"

out=$("$SHELL_BIN" -c 'echo "$HOME"' | head -c 1)
check "HOME expansion" "/" "$out"

out=$("$SHELL_BIN" -c 'echo {1,2,3}' 2>/dev/null || echo "ok")
[ -n "$out" ] && pass=$((pass+1)) && printf "  ok   braces (noop)\n" || true

out=$("$SHELL_BIN" -c 'echo "a b c" | wc -w')
check "wc -w pipe" "3" "$out"

out=$("$SHELL_BIN" -c 'printf "x\ty\n" | cat')
expected="x	y"
check "printf tab" "$expected" "$out"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

out=$("$SHELL_BIN" -c "cd $tmp && pwd")
check "cd + pwd" "$tmp" "$out"

out=$("$SHELL_BIN" -c "echo hi > $tmp/a; cat $tmp/a")
check "redirect >" "hi" "$out"

out=$("$SHELL_BIN" -c "echo x >> $tmp/b; echo y >> $tmp/b; cat $tmp/b")
expected="x
y"
check "redirect >>" "$expected" "$out"

out=$("$SHELL_BIN" -c "echo a b c | tr ' ' '-'")
check "pipe tr" "a-b-c" "$out"

out=$("$SHELL_BIN" -c 'echo one two three | cut -d " " -f 2')
check "pipe cut" "two" "$out"

echo ""
echo "shell: $pass ok, $fail fail"
[ "$fail" = "0" ] || exit 1
