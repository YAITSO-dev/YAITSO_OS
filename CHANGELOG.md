# Changelog

## [0.3.1] — 2026-10-10

### Added
- glibc 2.40 в /usr/glibc как слой совместимости
- Утилита yglibc (run/patch/verify/info)
- patchelf
- 11 пакетов glibc-*
- firefox (ESR 128) в репо
- yterm: вкладки (Ctrl+Shift+T/W, Alt+1..9)
- yterm: поиск в буфере (Ctrl+Shift+F)
- yterm: bracketed paste mode
- yterm: bell → подсветка вкладки
- yterm: --test-color
- installer: вопрос про glibc
- docs/glibc.md
- tests/glibc.sh

### Fixed
- ycomp: segfault в request_cursor
- ycomp: hot-plug клавиатуры
- ycomp IPC: права 0600 + SO_PEERCRED
- ybar: не виснет при мёртвом ycomp
- yterm: скролл истории
- libywt: динамическая версия wl_output
- login: lock после 3 попыток
- Makefile: порядок сборки ytui/tools

### Changed
- build-pkg.sh: поддержка LIBC=glibc и BUILD_SYSTEM=glibc/glibc-libs
- yat/yat.h: struct pkg_meta с полем libc
- mkiso.sh: копирует /usr/glibc и yglibc

## [0.3.0] — 2026-10-09
- GUI: ycomp, yterm, ybar, ylaunch, libywt
- HTTPS в yat
- start-desktop
- 22 новых пакета

## [0.2.3] — 2026-10-09
- Исправление багов 0.2.2

## [0.2.0] — 2026-10-09
- ybox, ywifi, svc sandbox, yat why/graph/pin/rollback/verify

## [0.1.0] — 2026-10-09
- Первая ISO

## [0.0.1] — 2026-10-08
- Минимальная оболочка
