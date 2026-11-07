#!/bin/sh
# Сборка графической среды Yaitso OS на хосте с wlroots 0.18.
# Результат: gui-out/usr/bin/{ycomp,ybar,ylaunch,yterm,start-desktop}
# Требуются: gcc, make, pkg-config, wlroots-0.18, wayland-client/server, xkbcommon, freetype2.
set -eu
cd "$(dirname "$0")"

if ! pkg-config --exists wlroots-0.18; then
    echo "wlroots-0.18 не найден через pkg-config." >&2
    echo "Если он в /usr/local, попробуйте:" >&2
    echo "  export PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig:/usr/local/lib/pkgconfig" >&2
    exit 1
fi
for m in wayland-client wayland-server xkbcommon freetype2; do
    pkg-config --exists "$m" || { echo "нет пакета pkg-config: $m" >&2; exit 1; }
done
echo "wlroots $(pkg-config --modversion wlroots-0.18)"

make -C libywt
make -C ycomp
make -C ybar
make -C ylaunch
make -C yterm
make -C start-desktop

OUT=gui-out/usr/bin
rm -rf gui-out
mkdir -p "$OUT"
cp ycomp/ycomp ybar/ybar ylaunch/ylaunch yterm/yterm start-desktop/start-desktop "$OUT"/

echo
echo "Готово: $OUT"
echo "Библиотеки, которые нужны ycomp (NEEDED), и их soname на хосте:"
readelf -d "$OUT/ycomp" | grep NEEDED | sed 's/.*\[\(.*\)\]/  \1/'
readelf -d "$(pkg-config --variable=libdir wlroots-0.18 2>/dev/null)/libwlroots-0.18.so" 2>/dev/null | grep SONAME || true
echo "В образе ycomp ожидает libwlroots-0.18.so (SONAME). Если на хосте soname другой,"
echo "переименуйте библиотеку под NEEDED при копировании в образ."
