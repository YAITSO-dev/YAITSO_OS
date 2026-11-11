# Yaitso OS 0.3.2

Yet Another Independent Typical Stable Origin OS

## Что нового в 0.3.2

Версия 0.3.2 — выпуск исправлений: безопасность, стабильность и корректная
работа оболочки, GUI и системных утилит.

### Безопасность
- login/passwd/su/sudo: настоящая проверка пароля через `/etc/shadow`
  (`getspnam` + `crypt`), запрет пустых и заблокированных хешей
- login: проверка имени пользователя и безопасные пути ban-файлов
- passwd: проверка старого пароля, атомарная запись shadow (0600, root)
- sudo: сброс дополнительных групп через `setgroups(0, NULL)`
- useradd/userdel: атомарная запись passwd/group/shadow, проверка имён,
  удаление домашнего каталога без `system()`
- yat/svc/yglibc: проверка имён, путей и границ буферов

### Оболочка yaitso
- корректный разбор редиректов: `2>&1`, `1>&2`, `2>`, `2>>`, `1>`, `1>>`,
  `&>`, `&>>`, `>`, `>>`, `<`, кавычки и экранирование
- фоновые команды через `&`, конвейеры больше не «съедают» фоновые задания
- встроенные команды работают внутри конвейера
- позиционные параметры `$0..$9`, `$#`, `$@`, `$*`, `${N}`, `${#}`
- `-c [--] команда` и передача аргументов скрипту
- `printf`: backslash-последовательности и повтор формата
- `tail -n 0` и отрицательные значения без деления на ноль
- histsize реально ограничивает историю
- UTF-8: безопасный подсчёт длины обрезанных последовательностей

### Графика и GUI
- ycomp: создаётся `wl_compositor`/`wl_subcompositor`/data-device — окна
  клиентов действительно появляются, а не остаются чёрным экраном
- ycomp: первичный configure xdg-окон и layer-surface, панель ybar на всю
  ширину сверху, корректный атомарный DRM-режим и первый кадр
- ycomp: резервный ввод напрямую из `/dev/input/event*`, если backend не
  создал устройств (минимальный rootfs без udev/libinput)
- ycomp: встроенная раскладка US, если в системе нет `/usr/share/X11/xkb`
- libywt: слушатель `wl_seat` вешается до события `capabilities`, поэтому
  клавиатура и указатель клиентов работают; разбор keymap без include-путей
- ybar: подсветка активного окна через IPC (id + focus)
- ylaunch: `%%` в .desktop и правильная передача argv в `yterm -e`
- ytui: корректный цвет фона и неблокирующий ESC
- start-desktop: поиск любого `/dev/dri/cardN` с ожиданием до 10 секунд,
  автозапуск yterm и журнал `/var/log/ycomp.log`
- init/mkiso: загрузка `psmouse`/`usbhid` — мышь и клавиатура в QEMU и на
  обычном железе
- yterm: UTF-8 и CSI-последовательности, переживающие границы `feed()`

### Утилиты
- ybox: собственные рекурсивные `cp` и `chown` (имена пользователей/групп)
- yat: рабочий `upgrade` (сравнение версий, только более новые пакеты)
- svc: запуск сервисов от указанного пользователя, проверка имени сервиса
- yglibc verify: поиск зависимостей по всему файлу
- pbkdf2: защита от переполнения при длинной соли

### Из 0.3.1
- glibc 2.40 в `/usr/glibc`, утилита `yglibc`, пакеты `glibc-*`
- `firefox` (ESR 128) через `yat install firefox`
- yterm: вкладки, поиск, bracketed paste, bell, `--test-color`


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

Этот проект распространяется под лицензией
[GNU General Public License версии 2 (GPLv2)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
