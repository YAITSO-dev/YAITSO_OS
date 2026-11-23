#!/bin/bash
set -eu
DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$DIST_DIR/yaitso-0.3.31.iso"
DISK="${DISK:-$DIST_DIR/build/disk.img}"
MEM="${MEM:-1024}"
CPUS="${CPUS:-2}"
MODE="${1:-bios}"

[ -f "$ISO" ] || { echo "нет iso: $ISO" >&2; exit 1; }
[ -f "$DISK" ] || qemu-img create -f raw "$DISK" 8G

KVM=""
[ -w /dev/kvm ] && KVM="-enable-kvm -cpu host"

run_bios() {
    exec qemu-system-x86_64 $KVM -m "$MEM" -smp "$CPUS" \
        -drive file="$DISK",format=raw,if=virtio \
        -drive file="$ISO",media=cdrom,readonly=on,if=none,id=cd0 \
        -device ide-cd,drive=cd0,bus=ide.1 \
        -boot order=d,menu=on \
        -vga std -vnc :1 \
        -netdev user,id=n0 -device e1000,netdev=n0 -serial mon:stdio
}

run_uefi() {
    OVMF_CODE=""; OVMF_VARS=""
    for p in /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE_4M.fd \
             /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
        [ -f "$p" ] && OVMF_CODE="$p" && break
    done
    for p in /usr/share/OVMF/OVMF_VARS.fd /usr/share/OVMF/OVMF_VARS_4M.fd \
             /usr/share/edk2-ovmf/x64/OVMF_VARS.fd; do
        [ -f "$p" ] && OVMF_VARS="$p" && break
    done
    if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS" ]; then
        echo "run-qemu: OVMF не найден, запускаю BIOS" >&2
        run_bios
    fi
    VARSCOPY="$DIST_DIR/build/OVMF_VARS.fd"
    [ -f "$VARSCOPY" ] || cp "$OVMF_VARS" "$VARSCOPY"
    exec qemu-system-x86_64 $KVM -m "$MEM" -smp "$CPUS" \
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,file="$VARSCOPY" \
        -drive file="$DISK",format=raw,if=virtio \
        -drive file="$ISO",media=cdrom,readonly=on,if=none,id=cd0 \
        -device ide-cd,drive=cd0,bus=ide.1 \
        -boot order=d,menu=on \
        -vga std -vnc :1 \
        -netdev user,id=n0 -device e1000,netdev=n0 -serial mon:stdio
}

run_gui() {
    exec qemu-system-x86_64 $KVM -m "${MEM:-2048}" -smp "$CPUS" \
        -machine q35 \
        -drive file="$DISK",format=raw,if=virtio \
        -drive file="$ISO",media=cdrom,readonly=on,if=none,id=cd0 \
        -device ide-cd,drive=cd0,bus=ide.1 \
        -boot order=d,menu=on \
        -vga std \
        -vnc :1 \
        -device virtio-keyboard-pci \
        -device virtio-mouse-pci \
        -netdev user,id=n0 -device e1000,netdev=n0 \
        -serial mon:stdio
}

case "$MODE" in
    uefi) run_uefi ;;
    gui)  run_gui ;;
    bios|*) run_bios ;;
esac
