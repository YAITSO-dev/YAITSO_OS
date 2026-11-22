#!/bin/bash
set -eu

DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PKG_DIR="$1"

[ -f "$PKG_DIR/PKG" ] || { echo "build-meson: нет PKG в $PKG_DIR" >&2; exit 1; }
. "$PKG_DIR/PKG"

OUT_DIR="$DIST_DIR/repo"
mkdir -p "$OUT_DIR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SRCDIR="$WORK/src"
BUILDDIR="$WORK/build"
STAGE="$WORK/stage"
mkdir -p "$SRCDIR" "$BUILDDIR" "$STAGE"

[ -f "$PKG_DIR/src.tar.gz" ] || { echo "build-meson: нет $PKG_DIR/src.tar.gz" >&2; exit 1; }

echo ">>> extract $NAME-$VERSION"
tar -xzf "$PKG_DIR/src.tar.gz" -C "$SRCDIR" --strip-components=1

command -v meson >/dev/null 2>&1 || { echo "build-meson: meson не найден" >&2; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "build-meson: ninja не найден" >&2; exit 1; }

MESON_OPTS="${MESON_OPTS:-}"

echo ">>> setup $NAME-$VERSION"
meson setup "$BUILDDIR" "$SRCDIR" \
    --prefix=/usr \
    --buildtype=release \
    --default-library=shared \
    -Dwerror=false \
    $MESON_OPTS

echo ">>> build $NAME-$VERSION"
ninja -C "$BUILDDIR"

echo ">>> install -> stage"
DESTDIR="$STAGE" ninja -C "$BUILDDIR" install

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

cp "$FILES_LIST" "$WORK/.FILES"

TARBALL="$OUT_DIR/$NAME-$VERSION.yat"
( cd "$WORK" && tar -czf "$TARBALL" META .FILES -C "$STAGE" . )
echo ">>> done: $TARBALL"
