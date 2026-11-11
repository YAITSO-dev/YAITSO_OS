# Yaitso OS 0.3.2 — графическая среда

## Обзор

GUI в Yaitso OS 0.3.2 — опциональный. Состоит из четырёх компонентов,
построенных на wlroots 0.18:

- **ycomp** — композитор (~1700 строк)
- **yterm** — терминал (pty + VT100/ECMA-48, ~1000 строк)
- **ybar** — панель (обычное xdg-окно с `app_id=ybar.yaitso`, ~500 строк)
- **ylaunch** — лаунчер (читает .desktop, ~450 строк)

Общая библиотека `libywt` (~1050 строк) инкапсулирует работу с
`wl_display`, `wl_shm`, `xdg_shell`, `wl_seat`, xkbcommon, шрифтами
(FreeType) и рисованием.

## Включение

    echo desktop > /etc/yaitso/session

При следующем логине `login` запустит `/usr/bin/start-desktop`. Он
поднимает `ycomp`, панель `ybar` и автоматически открывает `yterm`.
`init` при `desktop` запускает графическую сессию на tty1, а на
tty2–tty4 поднимает getty для текстового fallback.

`start-desktop` пишет stderr композитора в `/var/log/ycomp.log` — это
основной журнал при разборе проблем с DRM и вводом.

## Ввод

Обычно устройства ввода приходят от backend wlroots (libinput). В
минимальном rootfs YAITSO нет udev-базы и `/usr/share/libinput`, поэтому
при `WLR_BACKENDS=drm` композитор сам читает `/dev/input/event*`:

- клавиатуры → `wlr_keyboard` + группа клавиатур;
- мыши, тачпады и планшеты (относительные и абсолютные) → `wlr_cursor`;
- колесо и кнопки транслируются в события `wl_seat`.

Повторные сканирования через 0,5 с подхватывают устройства, появившиеся
после старта композитора (например, поздно загруженный `psmouse`).
Принудительно включить или отключить evdev можно переменной
`YCOMP_EVDEV=1` / `YCOMP_EVDEV=0`.

Если в системе нет `/usr/share/X11/xkb`, используется встроенная
раскладка US (pc105). При наличии xkb-данных они имеют приоритет.

`init` загружает модули `psmouse` и `usbhid`, а `build/mkiso.sh`
включает их в initramfs вместе с зависимостями.

## Диагностика

- `/var/log/ycomp.log` — запуск backend, DRM-режим, найденные evdev-устройства;
- `ycomp` печатает `ycomp: evdev ... (keyboard|pointer)` для каждого
  открытого устройства;
- проверить список устройств: `cat /proc/bus/input/devices`;
- при проблемах с раскладкой убедиться, что в журнале есть строка
  `no xkb data, using built-in US keymap`.

## IPC композитора

UNIX-сокет `/run/ycomp.sock`, текстовый протокол, права 0600,
проверка `SO_PEERCRED` (uid владельца или root).

| Команда | Ответ | Действие |
|---|---|---|
| `list` | `id\tapp_id\ttitle` × N | Список окон |
| `focus N` | `ok` | Фокус на окно N |
| `close N` | `ok` | Закрыть окно N |
| `status` | `ok\twindows=N` | Число окон |
| `terminal` | `ok` | Запустить yterm |
| `launcher` | `ok` | Запустить ylaunch |
| `exit` | `ok` | Завершить ycomp |

## Горячие клавиши

- **Super+Return** — терминал
- **Super+D** — лаунчер
- **Super+Q** — закрыть окно
- **Super+Shift+Q** — выход
- **Alt+Tab** — следующее окно
- **Super+LMB** — переместить окно
- **Super+RMB** — изменить размер

## Горячие клавиши yterm

- **Ctrl+Shift+T** — новая вкладка
- **Ctrl+Shift+W** — закрыть вкладку
- **Alt+1..9** — переключение вкладок
- **Ctrl+Shift+F** — поиск в буфере
- **Shift+PageUp/PageDown** — прокрутка истории
- **Ctrl+Shift+D / Ctrl+Escape** — выход
