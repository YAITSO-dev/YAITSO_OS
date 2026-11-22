#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="$DIST_DIR/build/rootfs"

[ "$(id -u)" = "0" ] || { echo "bootstrap: нужен root" >&2; exit 1; }

echo ">>> подготовка rootfs: $ROOTFS"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/bin "$ROOTFS"/sbin "$ROOTFS"/etc "$ROOTFS"/proc
mkdir -p "$ROOTFS"/sys "$ROOTFS"/dev "$ROOTFS"/tmp "$ROOTFS"/var
mkdir -p "$ROOTFS"/run "$ROOTFS"/usr "$ROOTFS"/root "$ROOTFS"/home
mkdir -p "$ROOTFS"/mnt "$ROOTFS"/opt "$ROOTFS"/boot "$ROOTFS"/boot
mkdir -p "$ROOTFS"/usr/bin "$ROOTFS"/usr/sbin "$ROOTFS"/usr/lib "$ROOTFS"/usr/share
mkdir -p "$ROOTFS"/var/log "$ROOTFS"/var/lib "$ROOTFS"/var/run
mkdir -p "$ROOTFS"/etc/yaitso
mkdir -p "$ROOTFS"/etc/yaitso/services
mkdir -p "$ROOTFS"/etc/yaitso/wifi
mkdir -p "$ROOTFS"/etc/yat
chmod 1777 "$ROOTFS/tmp"
chmod 700  "$ROOTFS/root"

if [ -d "$DIST_DIR/distro/airootfs" ]; then
    cp -R "$DIST_DIR/distro/airootfs/." "$ROOTFS/"
fi

for bin in yaitso yat/target/release/yat yat/target/release/yat-keygen \
           yat/target/release/yat-sign yhttpd/yhttpd \
           init/init svc/svc login/getty users/useradd users/userdel \
           users/passwd users/su users/sudo \
           ybox/ybox net/ywifi yaitso-control/yaitso-control; do
    [ -x "$DIST_DIR/$bin" ] && {
        name=$(basename "$bin")
        install -Dm755 "$DIST_DIR/$bin" "$ROOTFS/usr/bin/$name"
    }
done

# Команды ybox доступны и как отдельные имена (cp, rm, reboot, ...).
# busybox-ссылки в mkiso создаются только для отсутствующих имён, поэтому
# здесь получаются именно реализации ybox.
for c in true false yes basename dirname mkdir rmdir rm cp mv ln chmod chown \
         sync hostname uptime reboot poweroff touch df free mktemp realpath \
         readlink; do
    [ -e "$ROOTFS/usr/bin/$c" ] || ln -sf ybox "$ROOTFS/usr/bin/$c"
done

# Установщик ставим под отдельным именем: /usr/bin/install — это утилита
# coreutils, её нельзя перекрывать.
[ -x "$DIST_DIR/installer/install" ] && \
    install -Dm755 "$DIST_DIR/installer/install" \
        "$ROOTFS/usr/bin/yaitso-install"

install -Dm755 "$DIST_DIR/init/init" "$ROOTFS/sbin/init" 2>/dev/null || true
install -Dm755 "$DIST_DIR/login/getty" "$ROOTFS/sbin/getty" 2>/dev/null || true
install -Dm755 "$DIST_DIR/users/useradd" "$ROOTFS/usr/sbin/useradd" 2>/dev/null || true

ln -sf /usr/bin/yaitso "$ROOTFS/bin/sh"   2>/dev/null || true
ln -sf /usr/bin/yaitso "$ROOTFS/bin/bash" 2>/dev/null || true
ln -sf /usr/bin/yaitso "$ROOTFS/bin/ash"  2>/dev/null || true

ln -sf /sbin/init "$ROOTFS/init" 2>/dev/null || true

mkdir -p "$ROOTFS/repo"
cp -R "$DIST_DIR/repo/." "$ROOTFS/repo/" 2>/dev/null || true

[ -f "$ROOTFS/etc/passwd" ] || cat > "$ROOTFS/etc/passwd" <<EOF
root:x:0:0:root:/root:/bin/sh
nobody:x:65534:65534:nobody:/:/sbin/nologin
EOF

[ -f "$ROOTFS/etc/group" ] || cat > "$ROOTFS/etc/group" <<EOF
root:x:0:
wheel:x:10:
users:x:100:
nobody:x:65534:
EOF

[ -f "$ROOTFS/etc/shadow" ] || cat > "$ROOTFS/etc/shadow" <<'EOF'
root:$6$.XfrR5PSLVaMNmMc$yEj3.NcMKvab7yUHDb6djp/NuqDx8GuSqJ2g6U6aJ4JFim61DwQMQgzpGLLHSA53qa2XihusGUiGcR21xluZN1:19000:0:99999:7:::
nobody:!:19000:0:99999:7:::
EOF
chmod 640 "$ROOTFS/etc/shadow"

[ -f "$ROOTFS/etc/hostname" ] || echo "yaitso" > "$ROOTFS/etc/hostname"

[ -f "$ROOTFS/etc/fstab" ] || cat > "$ROOTFS/etc/fstab" <<EOF
proc     /proc  proc    defaults  0 0
sysfs    /sys   sysfs   defaults  0 0
devtmpfs /dev   devtmpfs defaults 0 0
tmpfs    /tmp   tmpfs   mode=1777 0 0
EOF

[ -f "$ROOTFS/etc/resolv.conf" ] || echo "nameserver 1.1.1.1" > "$ROOTFS/etc/resolv.conf"

[ -d "$ROOTFS/etc/yaitso/services" ] || mkdir -p "$ROOTFS/etc/yaitso/services"

echo ">>> rootfs готов"
du -sh "$ROOTFS"

echo "desktop" > "$ROOTFS/etc/yaitso/session"
