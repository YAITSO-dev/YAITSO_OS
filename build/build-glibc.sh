#!/bin/bash
set -eu

GLIBC_VER="${GLIBC_VER:-2.40}"
PREFIX="${PREFIX:-/usr/glibc}"
DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$DIST_DIR/build/glibc-build"
SRC_TAR="$DIST_DIR/third_party/glibc-$GLIBC_VER.tar.xz"
SRC_URL="https://ftp.gnu.org/gnu/glibc/glibc-$GLIBC_VER.tar.xz"
OUT_DIR="$DIST_DIR/repo"
STAGE="$WORK/stage"

mkdir -p "$DIST_DIR/third_party" "$OUT_DIR" "$WORK"
rm -rf "$STAGE"
mkdir -p "$STAGE"

if [ ! -f "$SRC_TAR" ]; then
    echo ">>> downloading glibc $GLIBC_VER"
    if command -v curl >/dev/null 2>&1; then
        curl -fL "$SRC_URL" -o "$SRC_TAR"
    else
        wget -O "$SRC_TAR" "$SRC_URL"
    fi
fi

cd "$WORK"
rm -rf "glibc-$GLIBC_VER" build
tar -xf "$SRC_TAR"
mkdir build
cd build

echo ">>> configure glibc $GLIBC_VER"
"../glibc-$GLIBC_VER/configure" \
    --prefix="$PREFIX" \
    --enable-kernel=4.19 \
    --enable-stack-protector=strong \
    --disable-werror \
    --disable-nscd \
    --without-selinux \
    --with-binutils="$(command -v ld)" \
    CFLAGS="-O2 -fno-strict-aliasing"

echo ">>> make glibc"
make -j"$(nproc)" 2>&1 | tail -5

echo ">>> install glibc -> stage"
make install DESTDIR="$STAGE" >/dev/null

echo ">>> prune"
rm -rf "$STAGE$PREFIX/share/i18n/locales"
rm -rf "$STAGE$PREFIX/share/i18n/charmaps"
rm -rf "$STAGE$PREFIX/share/info"
rm -rf "$STAGE$PREFIX/share/man"
rm -rf "$STAGE$PREFIX/share/doc"
rm -rf "$STAGE$PREFIX/var"
rm -rf "$STAGE$PREFIX/lib/audit"
find "$STAGE$PREFIX" -name '*.o' -delete
find "$STAGE$PREFIX" -name '*.a' -delete

FILES_LIST="$WORK/files.list"
( cd "$STAGE" && find . \( -type f -o -type l \) ) | sed 's|^\./||' | sort > "$FILES_LIST"
SIZE=$(du -sb "$STAGE" | cut -f1)

cat > "$WORK/META" <<EOF
name: glibc
version: $GLIBC_VER
arch: x86_64
size: $SIZE
depends:
desc: GNU C Library (compat layer for third-party apps)
EOF
cp "$FILES_LIST" "$WORK/.FILES"

TARBALL="$OUT_DIR/glibc-$GLIBC_VER.yat"
( cd "$WORK" && tar -czf "$TARBALL" META .FILES -C "$STAGE" . )
echo ">>> done: $TARBALL"
ls -lh "$TARBALL"
