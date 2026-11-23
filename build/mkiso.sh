#!/bin/bash
# Сборка live-ISO Yaitso OS 0.3.31.
#
# Использование:  sudo build/mkiso.sh           (или KERNEL=... KVER=... build/mkiso.sh)
#
# Зависимости хоста: gcc (+libc6-dev со static libc), cpio, gzip, kmod (depmod, modinfo),
#   busybox-static (/bin/busybox), zstd и/или xz (если модули в .zst/.xz),
#   grub-pc-bin + grub-efi-amd64-bin + grub-common + xorriso (для grub-mkrescue).
#
# KVER должен совпадать с версией ядра KERNEL: модули копируются из /lib/modules/$KVER.
# Копируем через cp -R, а не cp -a: сборка должна работать и без root
# (unshare -r), где сохранить исходного владельца файлов невозможно.
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="$DIST_DIR/build/rootfs"
ISODIR="$DIST_DIR/build/iso"
OUT="$DIST_DIR/yaitso-0.3.31.iso"
KERNEL="${KERNEL:-/boot/vmlinuz}"
KVER="${KVER:-$(uname -r)}"
MODDIR="/lib/modules/$KVER"
DEST="$ROOTFS/lib/modules/$KVER"
BUSYBOX=/bin/busybox

die()  { echo "mkiso: ОШИБКА: $*" >&2; exit 1; }
warn() { echo "mkiso: предупреждение: $*" >&2; }
need() { command -v "$1" >/dev/null 2>&1 || die "не найдена утилита '$1'"; }

# Копирует разделяемые библиотеки, от которых зависит бинарник. Нужно для
# установщика и его утилит (parted, e2fsprogs, dosfstools, grub).
copy_libs_for() {
    local bin="$1" lib dir
    [ -f "$bin" ] || return 0
    ldd "$bin" 2>/dev/null | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' |
    sort -u | while read -r lib; do
        [ -f "$lib" ] || continue
        # Уже скопированную библиотеку не трогаем, но её зависимости
        # обрабатываются при первом копировании.
        [ -f "$ROOTFS$lib" ] && continue
        dir="$ROOTFS$(dirname "$lib")"
        mkdir -p "$dir"
        cp -L "$lib" "$dir/" 2>/dev/null || true
        copy_libs_for "$lib"
    done
}

# Копирует утилиту в тот же путь внутри rootfs и её библиотеки.
copy_tool() {
    local name="$1" path real
    path="$(command -v "$name" 2>/dev/null || true)"
    if [ -z "$path" ]; then
        warn "утилита $name не найдена на хосте"
        return 0
    fi
    real="$(readlink -f "$path")"
    install -Dm755 "$real" "$ROOTFS$path"
    copy_libs_for "$real"
}

# ---- проверки окружения -----------------------------------------------------
for t in gcc cpio gzip depmod modinfo; do need "$t"; done
[ -d "$ROOTFS" ]  || die "нет rootfs: $ROOTFS (сначала подготовьте систему в build/rootfs)"
[ -f "$KERNEL" ]  || die "kernel не найден: $KERNEL"
[ -d "$MODDIR" ]  || die "нет модулей для ядра $KVER: $MODDIR"
[ -x "$BUSYBOX" ] || die "нет $BUSYBOX (apt install busybox-static)"

# ---- каталоги образа ---------------------------------------------------------
rm -rf "$ISODIR"
mkdir -p "$ISODIR/boot/grub" "$ISODIR/EFI/BOOT" "$ISODIR/isolinux"
mkdir -p "$ROOTFS"/{boot,sbin,bin,usr/bin,usr/sbin,lib,lib64,etc,dev,proc,sys,run,tmp,var/log,home,root,mnt,repo}

cp "$KERNEL" "$ISODIR/boot/vmlinuz"

# Конфиги дистрибутива (session, desktop.conf, tty.conf, services, ...)
if [ -d "$DIST_DIR/distro/airootfs" ]; then
    echo ">>> конфиги из distro/airootfs"
    cp -R "$DIST_DIR/distro/airootfs/." "$ROOTFS/"
fi

if [ -d "$DIST_DIR/repo" ]; then
    cp -R "$DIST_DIR/repo/." "$ROOTFS/repo/"
fi

# ---- init: собираем из init/init.c статически и ставим в rootfs ---------------
# Раньше init в образ из исходников не попадал, поэтому правки init.c на ISO
# не влияли. Теперь /sbin/init всегда берётся из init/init.c.
echo ">>> сборка init из init/init.c"
mkdir -p "$DIST_DIR/build"
gcc -static -O2 -Wall -o "$DIST_DIR/build/init" "$DIST_DIR/init/init.c" \
    || die "init/init.c не собрался"
install -Dm755 "$DIST_DIR/build/init" "$ROOTFS/sbin/init"
install -Dm755 "$DIST_DIR/build/init" "$ROOTFS/usr/bin/init"

# ---- бинарники GUI ------------------------------------------------------------
echo ">>> копирование бинарников системы"
for bin in ycomp yterm ybar ylaunch; do
    if [ -x "$DIST_DIR/$bin/$bin" ]; then
        install -Dm755 "$DIST_DIR/$bin/$bin" "$ROOTFS/usr/bin/$bin"
    else
        warn "нет собранного $bin ($DIST_DIR/$bin/$bin)"
    fi
done
if [ -x "$DIST_DIR/start-desktop/start-desktop" ]; then
    install -Dm755 "$DIST_DIR/start-desktop/start-desktop" "$ROOTFS/usr/bin/start-desktop"
else
    warn "нет собранного start-desktop: GUI не запустится"
fi
if [ -x "$DIST_DIR/tools/yglibc" ]; then
    install -Dm755 "$DIST_DIR/tools/yglibc" "$ROOTFS/usr/bin/yglibc"
fi

# ---- wlroots и разделяемые библиотеки ----------------------------------------
echo ">>> копирование GUI-библиотек (wlroots и зависимости)"
mkdir -p "$ROOTFS/lib/x86_64-linux-gnu" "$ROOTFS/usr/local/lib/x86_64-linux-gnu" "$ROOTFS/lib64"
cp -L /usr/local/lib/x86_64-linux-gnu/*.so* "$ROOTFS/usr/local/lib/x86_64-linux-gnu/" 2>/dev/null || true

for bin in "$DIST_DIR"/ycomp/ycomp "$DIST_DIR"/ybar/ybar "$DIST_DIR"/yterm/yterm "$DIST_DIR"/ylaunch/ylaunch; do
    [ -f "$bin" ] || continue
    ldd "$bin" 2>/dev/null | awk '{print $3}' | grep '^/' | while read -r lib; do
        [ -f "$lib" ] || continue
        cp -L "$lib" "$ROOTFS/lib/x86_64-linux-gnu/" 2>/dev/null || true
        ldd "$lib" 2>/dev/null | awk '{print $3}' | grep '^/' | while read -r sublib; do
            [ -f "$sublib" ] || continue
            cp -L "$sublib" "$ROOTFS/lib/x86_64-linux-gnu/" 2>/dev/null || true
        done
    done || true
done

cp -L /lib64/ld-linux-x86-64.so.2 "$ROOTFS/lib64/" 2>/dev/null || true
cp -L /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$ROOTFS/lib/x86_64-linux-gnu/" 2>/dev/null || true

# ---- библиотеки системных программ ------------------------------------------
# getty/login/passwd/su/sudo собраны динамически и используют libcrypt.
# Без libcrypt.so.1 в live-системе getty не запускается, и автовход/вход
# в систему невозможен.
echo ">>> библиотеки системных программ"
for bin in "$DIST_DIR/login/getty" "$DIST_DIR/users/useradd" \
           "$DIST_DIR/users/userdel" "$DIST_DIR/users/passwd" \
           "$DIST_DIR/users/su" "$DIST_DIR/users/sudo" \
           "$DIST_DIR/yaitso" "$DIST_DIR/ybox/ybox" "$DIST_DIR/svc/svc" \
           "$DIST_DIR/yat/target/release/yat" "$DIST_DIR/yaitso-control/yaitso-control"; do
    [ -f "$bin" ] && copy_libs_for "$bin"
done

# ---- busybox (modprobe/insmod) -----------------------------------------------
echo ">>> busybox, modprobe"
install -Dm755 "$BUSYBOX" "$ROOTFS/sbin/busybox"
ln -sf busybox "$ROOTFS/sbin/modprobe"
ln -sf busybox "$ROOTFS/sbin/insmod"

# В live-rootfs почти нет отдельных утилит, а yaitso/yat вызывают cp, tar,
# find, grep и т.п. через PATH. Делаем ссылки на applet'ы busybox, не перетирая
# собственные бинарники Yaitso.
echo ">>> ссылки на applet'ы busybox"
# bootstrap раньше ставил установщик как /usr/bin/install и перекрывал
# одноимённую утилиту (нужна пакетам). Теперь он всегда yaitso-install.
[ -f "$ROOTFS/usr/bin/install" ] && [ ! -L "$ROOTFS/usr/bin/install" ] && \
    rm -f "$ROOTFS/usr/bin/install"

for app in $("$BUSYBOX" --list); do
    case "$app" in
        init|linuxrc|sh|bash|ash|modprobe|insmod|rmmod|lsmod|depmod|\
        poweroff|reboot|halt|login|passwd|su|mdev|switch_root|\
        getty|vlock|setpriv|chrt|ionice)
            continue ;;
    esac
    if [ ! -e "$ROOTFS/usr/bin/$app" ] && [ ! -L "$ROOTFS/usr/bin/$app" ]; then
        ln -sf /sbin/busybox "$ROOTFS/usr/bin/$app" 2>/dev/null || true
    fi
done
ln -sf /sbin/busybox "$ROOTFS/usr/bin/[" 2>/dev/null || true
ln -sf /sbin/busybox "$ROOTFS/usr/bin/[[" 2>/dev/null || true

# ---- установщик и утилиты, которые ему нужны ---------------------------------
# Установщик статический, но parted/mkfs/grub — динамические, поэтому копируем
# их вместе с библиотеками. Без этого установка с live-носителя невозможна.
echo ">>> установщик и его утилиты"
if [ -x "$DIST_DIR/installer/install" ]; then
    install -Dm755 "$DIST_DIR/installer/install" "$ROOTFS/usr/bin/yaitso-install"
else
    die "нет собранного installer/install (make -C installer)"
fi

for tool in parted sfdisk wipefs mkfs.ext4 mkfs.vfat blkid lsblk \
            grub-install grub-probe grub-mkdevicemap grub-bios-setup \
            grub-mkimage grub-editenv grub-mkconfig; do
    copy_tool "$tool"
done

if [ -d /usr/lib/grub ]; then
    mkdir -p "$ROOTFS/usr/lib/grub"
    cp -R /usr/lib/grub/. "$ROOTFS/usr/lib/grub/" 2>/dev/null || true
fi
if [ -d /usr/share/grub ]; then
    mkdir -p "$ROOTFS/usr/share/grub"
    cp -R /usr/share/grub/. "$ROOTFS/usr/share/grub/" 2>/dev/null || true
fi

# grub-install — shell-скрипт. В target /bin/sh — это yaitso, поэтому кладём
# настоящий bash отдельным именем: установщик запускает им grub-install.
if [ -x /usr/bin/bash ]; then
    bash_real="$(readlink -f /usr/bin/bash)"
    install -Dm755 "$bash_real" "$ROOTFS/usr/bin/bash.real"
    copy_libs_for "$bash_real"
fi

# e2fsprogs читает /etc/mke2fs.conf; без него mkfs.ext4 может ругаться.
[ -f /etc/mke2fs.conf ] && \
    install -Dm644 /etc/mke2fs.conf "$ROOTFS/etc/mke2fs.conf"

# ---- модули ядра с зависимостями ----------------------------------------------
# Модуль копируется вместе со всеми модулями, от которых он зависит (поле depends
# из modinfo, рекурсивно). Зависимости, вкомпилированные в ядро (modules.builtin),
# пропускаются. Так не нужно вручную знать, что нужно virtio_gpu (например,
# virtio_dma_buf.ko, если он модульный на этом ядре).
echo ">>> копирование модулей ядра ($KVER): bochs, virtio_gpu и зависимости"
mkdir -p "$DEST"
declare -A SEEN=()

is_builtin() {
    local n="$1"
    [ -f "$MODDIR/modules.builtin" ] || return 1
    grep -qE "(^|/)$n\.ko\$" "$MODDIR/modules.builtin" && return 0
    grep -qE "(^|/)${n//_/-}\.ko\$" "$MODDIR/modules.builtin" && return 0
    grep -qE "(^|/)${n//-/_}\.ko\$" "$MODDIR/modules.builtin" && return 0
    return 1
}

# Имена модулей в ядре встречаются и с '_', и с '-'; find_module пробует оба.
find_module() {
    local name="$1" f
    f="$(find "$MODDIR" -name "$name.ko*" -print -quit 2>/dev/null)"
    [ -n "$f" ] || f="$(find "$MODDIR" -name "${name//_/-}.ko*" -print -quit 2>/dev/null)"
    [ -n "$f" ] || f="$(find "$MODDIR" -name "${name//-/_}.ko*" -print -quit 2>/dev/null)"
    printf '%s' "$f"
}

copy_module() {
    local name="$1" f rel deps dep
    [ -n "${SEEN[$name]+x}" ] && return 0
    SEEN[$name]=1
    if is_builtin "$name"; then
        echo "    = $name (builtin)"
        return 0
    fi
    f="$(find_module "$name")"
    [ -n "$f" ] || return 1
    rel="${f#"$MODDIR"/}"
    rel="${rel%.zst}"; rel="${rel%.xz}"; rel="${rel%.gz}"
    mkdir -p "$DEST/$(dirname "$rel")"
    case "$f" in
        *.zst) need zstd; zstd -dc "$f" > "$DEST/$rel" ;;
        *.xz)  need xz;   xz -dc "$f"   > "$DEST/$rel" ;;
        *.gz)  gzip -dc "$f" > "$DEST/$rel" ;;
        *)     cp "$f" "$DEST/$rel" ;;
    esac
    echo "    + $rel"
    deps="$(modinfo -F depends "$f" 2>/dev/null || true)"
    for dep in ${deps//,/ }; do
        copy_module "$dep" || die "модуль $name требует '$dep', которого нет ни в $MODDIR, ни в builtin"
    done
}

copy_module bochs || die "bochs.ko не найден в $MODDIR (нужен для QEMU -vga std)"
copy_module virtio_gpu || warn "virtio_gpu.ko не найден в $MODDIR — QEMU -vga virtio работать не будет"

# Ввод: PS/2 и USB мыши/клавиатуры. Без этих модулей в минимальном initramfs
# работают только уже встроенные в ядро AT-клавиатуры, а указателя нет.
for m in psmouse usbhid; do
    copy_module "$m" || warn "модуль $m не найден в $MODDIR — поддержка соответствующего ввода ограничена"
done

# Накопители: нужны загрузочному initramfs установленной системы, чтобы
# смонтировать корень на SATA/NVMe/USB. Встроенные в ядро драйверы
# пропускаются автоматически.
for m in ahci libahci nvme nvme-core virtio_blk virtio-pci sd_mod \
         usb-storage uas xhci_hcd xhci-pci ehci-pci ehci-hcd ohci-pci \
         ext4 jbd2 mbcache; do
    copy_module "$m" || true
done

# Прежний набор модулей, оставлен для совместимости (копируется, если модульный).
for m in drm_ttm_helper ttm drm_kms_helper drm fb syscopyarea sysfillrect sysimgblt fb_sys_fops; do
    copy_module "$m" || true
done

# depmod нужен для modprobe: без modules.dep busybox не найдёт модули по имени.
depmod -b "$ROOTFS" "$KVER" 2>/dev/null || true
[ -s "$DEST/modules.dep" ] || die "depmod не создал $DEST/modules.dep"
grep -q 'bochs\.ko' "$DEST/modules.dep" || die "bochs.ko нет в $DEST/modules.dep"

# ---- шрифты -------------------------------------------------------------------
mkdir -p "$ROOTFS/usr/share/fonts/truetype/dejavu"
if [ -f /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf ]; then
    cp /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf "$ROOTFS/usr/share/fonts/truetype/dejavu/"
else
    warn "нет DejaVuSansMono.ttf (терминал без шрифта)"
fi

# ---- ядро и initramfs ---------------------------------------------------------
# /boot/vmlinuz и /boot/initramfs.gz внутри live-системы нужны установщику:
# он копирует их в установленную систему. Загрузочный initramfs установленной
# системы минимальный: busybox + драйверы накопителей + /init, который
# монтирует корень и делает switch_root. Полный live-rootfs в него не входит.
echo ">>> установка ядра в образ"
install -Dm644 "$KERNEL" "$ROOTFS/boot/vmlinuz"

echo ">>> сборка загрузочного initramfs установленной системы"
INSTALL_IR="$DIST_DIR/build/install-initramfs"
rm -rf "$INSTALL_IR"
mkdir -p "$INSTALL_IR/bin" "$INSTALL_IR/sbin" "$INSTALL_IR/proc" \
         "$INSTALL_IR/sys" "$INSTALL_IR/dev" "$INSTALL_IR/run" \
         "$INSTALL_IR/newroot" "$INSTALL_IR/lib/modules/$KVER"
install -Dm755 "$BUSYBOX" "$INSTALL_IR/bin/busybox"
cp -R "$DEST/." "$INSTALL_IR/lib/modules/$KVER/"

cat > "$INSTALL_IR/init" <<'INSTALL_INIT'
#!/bin/busybox sh
export PATH=/bin:/sbin
/bin/busybox --install -s /bin 2>/dev/null

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mount -t tmpfs tmpfs /dev

# Драйверы накопителей; вкомпилированные в ядро modprobe просто пропустит.
for m in ahci libahci nvme nvme-core virtio_blk virtio-pci sd_mod \
         usb-storage uas xhci-hcd xhci-pci ehci-pci ehci-hcd ohci-pci \
         ext4 jbd2 mbcache; do
    modprobe "$m" 2>/dev/null
done

root=""
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        root=*) root="${arg#root=}" ;;
    esac
done
if [ -z "$root" ]; then
    echo "initramfs: параметр root= не задан"
    exec /bin/sh
fi

i=0
while [ ! -b "$root" ] && [ "$i" -lt 20 ]; do
    sleep 0.5
    i=$((i + 1))
done
if [ ! -b "$root" ]; then
    echo "initramfs: устройство $root не найдено"
    exec /bin/sh
fi

mkdir -p /newroot
if ! mount -t ext4 -o rw "$root" /newroot; then
    if ! mount -o rw "$root" /newroot; then
        echo "initramfs: не удалось смонтировать $root"
        exec /bin/sh
    fi
fi

for d in dev proc sys run; do
    [ -d "/newroot/$d" ] || mkdir -p "/newroot/$d"
    mount -o move "/$d" "/newroot/$d" 2>/dev/null
done

exec switch_root /newroot /sbin/init
INSTALL_INIT
chmod +x "$INSTALL_IR/init"

( set -o pipefail
  cd "$INSTALL_IR" && find . -print | cpio -o -H newc --quiet | gzip -9
) > "$ROOTFS/boot/initramfs.gz" || die "initramfs установленной системы не собрался"

# ---- конфиг GRUB --------------------------------------------------------------
mkdir -p "$ISODIR/boot/grub" "$ISODIR/EFI/BOOT"
cat > "$ISODIR/boot/grub/grub.cfg" <<MKGRUB
set timeout=3
set default=0
menuentry "Yaitso OS 0.3.31 (live)" {
    linux /boot/vmlinuz quiet console=tty0 console=ttyS0,115200n8 rdinit=/sbin/init video=efifb:off video=simpledrm:off
    initrd /boot/initramfs.gz
}
menuentry "Yaitso OS 0.3.31 (verbose)" {
    linux /boot/vmlinuz console=tty0 console=ttyS0,115200n8 rdinit=/sbin/init video=efifb:off video=simpledrm:off
    initrd /boot/initramfs.gz
}
menuentry "Yaitso OS 0.3.31 (install)" {
    linux /boot/vmlinuz quiet console=tty0 console=ttyS0,115200n8 rdinit=/sbin/init yaitso.install=1 video=efifb:off video=simpledrm:off
    initrd /boot/initramfs.gz
}
MKGRUB

cp "$ISODIR/boot/grub/grub.cfg" "$ISODIR/EFI/BOOT/grub.cfg"

if command -v grub-mkstandalone >/dev/null 2>&1; then
    grub-mkstandalone -O x86_64-efi -o "$ISODIR/EFI/BOOT/BOOTX64.EFI" \
        --modules="part_gpt part_msdos fat ext2 normal chain boot configfile linux search search_fs_file search_fs_uuid search_label all_video gfxterm efi_gop" \
        "boot/grub/grub.cfg=$ISODIR/boot/grub/grub.cfg"

    # Резервный EFI-загрузчик установленной системы: grub-install может не
    # сработать, тогда установщик кладёт этот файл в EFI/BOOT. Он не содержит
    # live-конфиг, а ищет установленный /boot/grub/grub.cfg.
    cat > "$ISODIR/EFI/BOOT/install-grub.cfg" <<'EFIINSTALL'
search --no-floppy --set=root --file /boot/grub/grub.cfg
configfile /boot/grub/grub.cfg
EFIINSTALL
    if grub-mkstandalone -O x86_64-efi \
            -o "$DIST_DIR/build/BOOTX64-install.efi" \
            --modules="part_gpt part_msdos fat ext2 normal configfile linux search search_fs_file search_fs_uuid search_label all_video gfxterm efi_gop" \
            "boot/grub/grub.cfg=$ISODIR/EFI/BOOT/install-grub.cfg"; then
        install -Dm644 "$DIST_DIR/build/BOOTX64-install.efi" \
            "$ROOTFS/usr/lib/yaitso/BOOTX64.EFI"
    fi
fi

# Финальный live-initramfs содержит всю live-систему и загрузочный
# initramfs установленной системы (в /boot/initramfs.gz).
( set -o pipefail
  cd "$ROOTFS" && find . -print | cpio -o -H newc --quiet | gzip -9
) > "$ISODIR/boot/initramfs.gz" || die "финальный initramfs не собрался"

# ---- ISO ----------------------------------------------------------------------
CORE_IMG="$DIST_DIR/build/core.img"
GRUB_OK=0

if grub-mkrescue -o "$OUT" "$ISODIR" 2>&1; then
    GRUB_OK=1
fi

if [ "$GRUB_OK" != "1" ]; then
    echo "mkiso: grub-mkrescue не сработал, пробую xorriso"
    need xorriso
    if [ -f /usr/lib/grub/i386-pc/cdboot.img ] && [ -d /usr/lib/grub/i386-pc ]; then
        if [ ! -f "$CORE_IMG" ]; then
            grub-mkimage -O i386-pc -o "$CORE_IMG" -p /boot/grub \
                biosdisk iso9660 part_gpt part_msdos fat ext2 normal chain boot configfile linux search search_fs_file search_fs_uuid search_label 2>/dev/null || true
        fi
        if [ -f "$CORE_IMG" ]; then
            cat /usr/lib/grub/i386-pc/cdboot.img "$CORE_IMG" > "$ISODIR/boot/grub/bios.img"
            xorriso -as mkisofs -o "$OUT" -R -J -V "YAITSO_OS" \
                -b boot/grub/bios.img -no-emul-boot -boot-load-size 4 -boot-info-table \
                -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
                -isohybrid-gpt-basdat "$ISODIR" || die "xorriso не собрал ISO"
        else
            warn "BIOS-образ не собрался, делаю UEFI-only"
            xorriso -as mkisofs -o "$OUT" -R -J -V "YAITSO_OS" \
                -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
                -isohybrid-gpt-basdat "$ISODIR" || die "xorriso не собрал ISO"
        fi
    else
        xorriso -as mkisofs -o "$OUT" -R -J -V "YAITSO_OS" \
            -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
            -isohybrid-gpt-basdat "$ISODIR" || die "xorriso не собрал ISO"
    fi
fi

echo ">>> iso готов: $OUT"
ls -lh "$OUT"
# SUDO_USER может быть не задан (запуск без sudo); при set -u это надо обрабатывать явно.
if [ -n "${SUDO_USER:-}" ]; then
    chown "$SUDO_USER:$SUDO_USER" "$OUT" 2>/dev/null || true
fi
