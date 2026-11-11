# Changelog

## [0.3.2] — 2026-11-08

### Fixed — безопасность
- login/crypt_check: проверка `/etc/shadow` через `getspnam` + `crypt`,
  отказ для отсутствующих и заблокированных хешей
- login: проверка имени пользователя, безопасные пути ban-файлов,
  обработка NULL-путей
- passwd: проверка прав, старого пароля и запрет пустого пароля;
  атомарная запись shadow через `mkstemp` + `fsync` + `rename` (0600)
- su: аутентификация по shadow, поддержка `-`/`-l`, проверка аргументов
- sudo: аутентификация по shadow, сброс групп (`setgroups(0, NULL)`)
- useradd: атомарная запись passwd/group/shadow, проверка имён и групп,
  нативное редактирование `-G`
- userdel: атомарная фильтрация файлов, удаление из групп,
  рекурсивное удаление домашнего каталога через `nftw`
- installer: безопасная передача пароля в `chpasswd`, проверка ошибок
  mkfs/mount/useradd, суффикс `p` для NVMe/mmcblk
- svc: запуск от указанного пользователя, проверка имени сервиса
- yat: проверка имён и путей пакетов, `yglibc verify` читает файл целиком
- pbkdf2: динамический буфер для длинной соли

### Fixed — оболочка
- `parse_redirect`: кавычки, экранирование, `2>&1`, `1>&2`, `&>`, `&>>`
- exec: фоновые команды `&`, ожидание только своих процессов конвейера,
  встроенные команды в конвейере, `/dev/null` на stdin фоновых задач
- `-c [--]`, позиционные параметры `$0..$9`, `$#`, `$@`, `$*`
- printf: escape-последовательности и повтор формата
- tail: `-n <= 0` без деления на ноль
- histsize: реальное ограничение размера истории
- utf8: безопасный подсчёт обрезанных последовательностей
- config: поддержка одинарных кавычек
- readline: проверка ёмкости буфера при автодополнении

### Fixed — GUI и утилиты
- ycomp: создаются `wl_compositor`, `wl_subcompositor` и data-device-manager
  (без них клиенты не получали display и экран оставался чёрным)
- ycomp: первичный configure xdg-окон и layer-surface; панель ybar во всю
  ширину сверху; включён атомарный DRM и запрошен первый кадр
- ycomp: резервный ввод напрямую из evdev, если backend не создал устройств
  ввода (минимальный rootfs без udev/libinput); повторные сканирования
  подхватывают поздно появившиеся мыши
- ycomp: встроенная раскладка US при отсутствии `/usr/share/X11/xkb`
- libywt: `wl_seat` listener регистрируется сразу после bind — раньше
  событие capabilities терялось и клиенты не создавали wl_keyboard/wl_pointer
- libywt: xkb keymap разбирается с `XKB_CONTEXT_NO_DEFAULT_INCLUDES`
- start-desktop: автозапуск yterm, ожидание Wayland-сокета, журнал
  `/var/log/ycomp.log`
- init/mkiso: `psmouse` и `usbhid` в initramfs и загрузка при старте
- ycomp: отдельный список layer-surface, клавиатурный фокус активного окна,
  сброс фокуса при unmap/destroy
- ybar: подсветка активного окна через IPC
- ylaunch: `%%` в .desktop, правильные argv для `yterm -e`
- ytui: цвет фона, неблокирующий ESC
- start-desktop: поиск любого `/dev/dri/cardN`, ожидание до 10 секунд
- yterm: UTF-8 и CSI через границы `feed()`, Ctrl+буква, reap зомби
- ybox: собственные `cp -r` и `chown` (имена и числовые id)
- yat: исправлен `upgrade` (сравнение версий, только более новые)
- ycomp/ybar: IPC передаёт id и признак фокуса окна

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
