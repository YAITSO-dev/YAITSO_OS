#!/bin/sh
# Запуск образа Yaitso OS в QEMU (Linux, нужен qemu-system-x86_64).
# Использование: ./run-qemu.sh [путь/к/yaitso-0.3.1-fixed.iso] [bios|uefi]
# Графика: -vga std даёт bochs-совместимый дисплей, которого ждёт драйвер bochs.ko.
set -eu
ISO="${1:-yaitso-0.3.1-fixed.iso}"
MODE="${2:-bios}"
ARGS="-m 2048 -smp 2 -cdrom $ISO -boot d -vga std -display gtk -no-reboot -name yaitso"

if [ -w /dev/kvm ] 2>/dev/null; then ARGS="$ARGS -enable-kvm"; fi

if [ "$MODE" = "uefi" ]; then
    OVMF=""
    for f in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd; do
        [ -f "$f" ] && OVMF="$f" && break
    done
    [ -n "$OVMF" ] || { echo "Нет OVMF (пакет ovmf)" >&2; exit 1; }
    ARGS="$ARGS -drive if=pflash,format=raw,readonly=on,file=$OVMF"
fi

echo "qemu-system-x86_64 $ARGS"
exec qemu-system-x86_64 $ARGS
