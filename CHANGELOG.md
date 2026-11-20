# Changelog

## [0.3.3] — 2026-11-18

### Added — рабочий стол
- ycomp: серверные заголовки окон с кнопками
  «свернуть/развернуть/закрыть»
- ycomp: перетаскивание окна за заголовок, изменение размера за края и углы
- ycomp: maximize/restore, привязка к половине экрана, сворачивание и
  восстановление, двойной щелчок по заголовку
- ycomp: горячие клавиши `Super+Return`, `Super+D`, `Super+Q`, `Super+M`,
  `Super+←/→`, `Super+H`, `Super+Tab`, `Alt+Tab`
- ycomp: градиентный фон рабочего стола, настраиваемые цвета
- ybar: кликабельные APPS и список окон, подсветка активного окна,
  приглушение свёрнутых, дата
- ylaunch: запуск мышью, прокрутка, PageUp/PageDown/Home/End, центрирование
  композитором, русские подписи
- IPC: команды `maximize`, `restore`, `toggle`, `minimize`, `unminimize`,
  `snap`, `next`, `prev`; `list` сообщает признак скрытого окна

### Added — установщик
- installer: полностью переписан; GPT (UEFI/BIOS), parted/sfdisk,
  mkfs.ext4/mkfs.vfat, fstab по UUID
- installer: копирование системы на C без внешних cp/rsync, сохранение
  прав, владельцев и символьных ссылок
- installer: пользователь, SHA-512 хеш, блокировка root, GRUB для UEFI/BIOS,
  резервный `BOOTX64.EFI`
- mkiso: отдельный минимальный initramfs установленной системы (busybox +
  драйверы накопителей) с `/init`, который монтирует корень и делает
  `switch_root` — установленная система больше не загружается из live-initramfs
- mkiso: модули ahci/nvme/virtio_blk/usb-storage/ext4 и зависимости
  попадают в live-систему и в загрузочный initramfs
- installer: `/etc/hosts`, подтверждение пароля, исправлена запись
  `/etc/shadow` для длинных файлов
- installer: параметры `--list`, `--disk`, `--hostname`, `--user`,
  `--password`, `--gui/--no-gui`, `--efi/--bios`, `--yes`, `--force`
- installer: статическая сборка, работает в минимальном live-rootfs
- mkiso: в образ копируются parted, sfdisk, e2fsprogs, dosfstools, grub,
  bash.real и их библиотеки
- GRUB: пункт «Yaitso OS 0.3.3 (install)» с `yaitso.install=1`

### Fixed — сессия и система
- init: всегда запускает getty/login; после logout снова появляется login
- login: авто­вход по `-f`/`-a`/`--autologin` и `/etc/yaitso/autologin`
  (live, только на tty1); установленная система файл удаляет. Ограничение
  tty1 устраняет одновременный запуск нескольких DRM-сессий и OOM
- init: режим установщика с tty1 и возврат к login после завершения
- start-desktop: `XDG_RUNTIME_DIR`, Wayland-переменные, ожидание сокета
  композитора, журнал в runtime-каталоге пользователя
- mkiso: ссылки на applet'ы busybox (cp, tar, find, grep, df, free, touch,
  mktemp, realpath и др.)
- mkiso: ядро и initramfs внутри live-системы, двухпроходная сборка
  initramfs для установщика
- bootstrap: установщик ставится как `yaitso-install`, а не перекрывает
  `/usr/bin/install`

### Fixed — рабочий стол
- окно больше не «уезжает» и не дёргается при перетаскивании/ресайзе
- панель не забирает клавиатурный фокус
- ycomp отправляет первый configure панели и лаунчеру; libywt не
  прикрепляет буфер до configure — устранено падение ybar с
  `xdg_surface has never been configured`
- окна не выходят за рабочую область, заголовок остаётся доступным
- `list` в IPC не ломался на окнах без заголовка
- IPC-сокет создаётся в `$XDG_RUNTIME_DIR` — ybar работает от обычного
  пользователя, а не только от root
- mkiso: копируются `libcrypt.so.1` и библиотеки getty/login/passwd/su/sudo;
  без этого вход в систему и автовход не работали
- init: `/run/user` создаётся с правами 1777, а `/dev/dri`, `/dev/input`,
  `/dev/snd` получают группы video/input/render/audio — графическая сессия
  обычного пользователя больше не падает с `Permission denied`
- distro: стандартные группы (`video`, `input`, `render`, `audio`, `tty`)
- installer/useradd: пользователь добавляется в `wheel,video,input,render,audio`;
  `useradd` понимает `-s` и записывает указанную оболочку
- installer: `grub-install` запускается напрямую, если это ELF-бинарник,
  и через `bash.real`, если это скрипт (раньше BIOS-установка падала с
  `cannot execute binary file`)
- yterm: история и вкладки уменьшены до разумного статического размера
  (4 вкладки × 1000 строк); прежние 16 × 2000 давали ~300 МБ RSS
- ycomp: третья кнопка заголовка — «свернуть»

### Changed
- версия 0.3.3 во всех пакетах, `--version`, man-страницах и документации
- репозиторий очищен от собранных бинарников: исполняемые файлы, `*.o`,
  `*.a` и `gui-out/` больше не хранятся в git и пересобираются `make` /
  `make gui`

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

### Лицензия
- проект переведён с CC BY-NC-SA 4.0 на GNU General Public License v2
  (GPLv2); полный текст — в файле `LICENSE`

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
