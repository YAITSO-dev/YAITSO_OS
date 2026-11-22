#!/bin/sh
set -eu
[ $# -lt 1 ] && { echo "usage: build-pkg.sh <pkg-dir>" >&2; exit 1; }
DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PKG_DIR="$1"
[ -f "$PKG_DIR/PKG" ] || { echo "нет PKG в $PKG_DIR" >&2; exit 1; }

BUILD_SYSTEM="make"
LIBC=""
. "$PKG_DIR/PKG"

case "${BUILD_SYSTEM:-make}" in
    meson)
        exec "$DIST_DIR/build/build-meson.sh" "$PKG_DIR"
        ;;
    glibc)
        exec "$DIST_DIR/build/build-glibc.sh"
        ;;
    glibc-libs)
        exec "$DIST_DIR/build/build-glibc-libs.sh" "$PKG_DIR"
        ;;
    cargo)
        ;;
    configure|copy|perl|make|"")
        ;;
    *)
        echo "build-pkg: неизвестный BUILD_SYSTEM: $BUILD_SYSTEM" >&2
        exit 1
        ;;
esac

OUT_DIR="$DIST_DIR/repo"
mkdir -p "$OUT_DIR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"
mkdir -p "$STAGE"

if [ "$BUILD_SYSTEM" = "cargo" ]; then
    CARGO_DIR="$DIST_DIR/yat"
    [ -f "$CARGO_DIR/Cargo.toml" ] || {
        echo "build-pkg: нет $CARGO_DIR/Cargo.toml" >&2
        exit 1
    }
    ( cd "$CARGO_DIR" && cargo build --release --offline --frozen )
    mkdir -p "$STAGE/usr/bin"
    for b in yat yat-keygen yat-sign; do
        [ -x "$CARGO_DIR/target/release/$b" ] || {
            echo "build-pkg: не собран $b" >&2
            exit 1
        }
        install -m 755 "$CARGO_DIR/target/release/$b" "$STAGE/usr/bin/$b"
    done
    for d in /var/lib/yat/installed /var/lib/yat/repo /var/cache/yat /etc/yat /var/log/yat; do
        mkdir -p "$STAGE$d"
    done
fi

if [ -d "$PKG_DIR/files" ]; then
    cp -a "$PKG_DIR/files/." "$STAGE/"
fi

if [ -n "${BUILD:-}" ] && [ "$BUILD_SYSTEM" != "cargo" ]; then
    ( cd "$PKG_DIR" && STAGE="$STAGE" DIST_DIR="$DIST_DIR" PKG_DIR="$PKG_DIR" sh -c "$BUILD" )
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
EOF

if [ -n "${LIBC:-}" ]; then
    echo "libc: $LIBC" >> "$WORK/META"
fi

cp "$FILES_LIST" "$WORK/.FILES"

TARBALL="$OUT_DIR/$NAME-$VERSION.yat"
( cd "$WORK" && tar -czf "$TARBALL" META .FILES -C "$STAGE" . )
echo ">>> done: $TARBALL"
