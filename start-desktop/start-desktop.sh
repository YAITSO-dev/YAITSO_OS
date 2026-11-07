#!/bin/sh
set -eu

if [ ! -e /dev/dri/card0 ] && [ ! -e /dev/dri/renderD128 ]; then
    echo "start-desktop: нет DRM-устройства, GUI невозможен" >&2
    exec /bin/yaitso
fi

UID_N=$(id -u)
export XDG_RUNTIME_DIR="/run/user/${UID_N}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=yaitso
export XDG_SESSION_DESKTOP=yaitso

# Важно: жёстко указываем, что рендерим через программный pixman и DRM
export WLR_BACKENDS=drm
export WLR_RENDERER=pixman
export WLR_NO_HARDWARE_CURSORS=1

(ybar >/dev/null 2>&1) &
exec ycomp
