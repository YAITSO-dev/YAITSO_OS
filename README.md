# Yaitso OS 0.3.1

Yet Another Independent Typical Stable Origin OS

## Что нового в 0.3.1

### Багфиксы
- ycomp: segfault в request_cursor, hot-plug клавиатуры
- ycomp IPC: права 0600 + проверка SO_PEERCRED
- ybar: не виснет при мёртвом ycomp
- yterm: корректный скролл истории
- libywt: динамическая версия wl_output
- login: lock после 3 попыток на 300 секунд
- Makefile: порядок сборки ytui/tools

### glibc-совместимость
- Пакет `glibc` (2.40) в `/usr/glibc`
- Утилита `yglibc`
- `patchelf` в репо
- `yat` читает `libc: glibc` из META
- Пакеты: `glibc-wayland-client`, `glibc-xkbcommon`, `glibc-freetype`,
  `glibc-fontconfig`, `glibc-dbus`, `glibc-glib`, `glibc-pango`,
  `glibc-nspr`, `glibc-nss`, `glibc-sqlite`, `glibc-icu`
- `firefox` (ESR 128) ставится через `yat install firefox`
- Установщик спрашивает про glibc

### Терминал
- Вкладки: Ctrl+Shift+T (новая), Ctrl+Shift+W (закрыть), Alt+1..9
- Поиск: Ctrl+Shift+F, Enter/Up/Down
- Bracketed paste mode
- Bell → подсветка вкладки
- `yterm --test-color`

## Сборка

    make
    make gui
    make test
    make check
    make repo repo-sign
    sudo make iso-gui
    make qemu-gui

## Установка Firefox

    yat install firefox
    firefox

## Требования хоста

    wayland, wayland-protocols, libdrm, libinput, libxkbcommon,
    pixman, wlroots (>= 0.18), freetype2, openssl, meson, ninja

## Известные ограничения

- glibc: только x86_64
- glibc: только passwd/group/shadow через файлы
- GUI: только Wayland, без XWayland
- VT100: без Sixel, mouse reporting

## Roadmap

- **0.4.0** — LUKS, свой SSH, heredoc, `$()`, if/for/while
- **0.5.0** — sed/awk, свой tar/gzip, воркспейсы ycomp
- **0.6.0** — без BusyBox, snapshot'ы ysnap
- **0.7.0** — XWayland, звук (PipeWire)

## Лицензия

Этот проект распространяется под лицензией [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
