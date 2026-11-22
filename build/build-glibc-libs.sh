#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PKG_DIR="$1"
[ -f "$PKG_DIR/PKG" ] || { echo "нет PKG в $PKG_DIR" >&2; exit 1; }
. "$PKG_DIR/PKG"

GLIBC_PREFIX=/usr/glibc
OUT_DIR="$DIST_DIR/repo"
mkdir -p "$OUT_DIR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SRCDIR="$WORK/src"
BUILDDIR="$WORK/build"
STAGE="$WORK/stage"
mkdir -p "$SRCDIR" "$BUILDDIR" "$STAGE"

[ -f "$PKG_DIR/src.tar.gz" ] || { echo "нет src.tar.gz в $PKG_DIR" >&2; exit 1; }
tar -xzf "$PKG_DIR/src.tar.gz" -C "$SRCDIR" --strip-components=1

CFLAGS_GLIBC="-O2 -fPIC -I/usr/glibc/include"
LDFLAGS_GLIBC="-L/usr/glibc/lib -Wl,-rpath,$GLIBC_PREFIX/lib -Wl,--dynamic-linker=$GLIBC_PREFIX/lib/ld-linux-x86-64.so.2"

SYSTEM="${GLIBC_BUILD_SYSTEM:-meson}"

case "$SYSTEM" in
meson)
    command -v meson >/dev/null 2>&1 || { echo "нет meson" >&2; exit 1; }
    command -v ninja >/dev/null 2>&1 || { echo "нет ninja" >&2; exit 1; }
    CFLAGS="$CFLAGS_GLIBC" LDFLAGS="$LDFLAGS_GLIBC" \
    meson setup "$BUILDDIR" "$SRCDIR" \
        --prefix=/usr \
        --buildtype=release \
        --default-library=shared \
        -Dwerror=false \
        ${MESON_OPTS:-}
    ninja -C "$BUILDDIR"
    DESTDIR="$STAGE" ninja -C "$BUILDDIR" install
    ;;
configure)
    cd "$BUILDDIR"
    CFLAGS="$CFLAGS_GLIBC" LDFLAGS="$LDFLAGS_GLIBC" \
        "$SRCDIR/configure" --prefix=/usr ${CONFIGURE_OPTS:-}
    make -j"$(nproc)"
    make install DESTDIR="$STAGE"
    ;;
custom)
    if [ -z "${BUILD:-}" ]; then
        echo "build-glibc-libs: BUILD_SYSTEM=custom требует BUILD" >&2
        exit 1
    fi
    ( cd "$PKG_DIR" && \
      SRCDIR="$SRCDIR" STAGE="$STAGE" DIST_DIR="$DIST_DIR" PKG_DIR="$PKG_DIR" \
      sh -c "$BUILD" )
    ;;
*)
    echo "build-glibc-libs: неизвестный GLIBC_BUILD_SYSTEM: $SYSTEM" >&2
    exit 1
    ;;
esac

if [ -d "$STAGE/usr/lib" ]; then
    mkdir -p "$STAGE$GLIBC_PREFIX/lib"
    find "$STAGE/usr/lib" -maxdepth 1 -type f -name '*.so*' \
        -exec cp -a {} "$STAGE$GLIBC_PREFIX/lib/" \;
fi

FILES_LIST="$WORK/files.list"
( cd "$STAGE" && find . \( -type f -o -type l \) ) | sed 's|^\./||' | sort > "$FILES_LIST"
SIZE=$(du -sb "$STAGE" | cut -f1)

cat > "$WORK/META" <<EOF
name: $NAME
version: $VERSION
arch: ${ARCH:-x86_64}
size: $SIZE
depends: ${DEPENDS:-}
desc: ${DESC:-}
libc: glibc
EOF
cp "$FILES_LIST" "$WORK/.FILES"

TARBALL="$OUT_DIR/$NAME-$VERSION.yat"
( cd "$WORK" && tar -czf "$TARBALL" META .FILES -C "$STAGE" . )
echo ">>> done: $TARBALL"
