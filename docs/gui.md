# Yaitso OS 0.3.1 — графическая среда

## Обзор

GUI в Yaitso OS 0.3.1 — опциональный. Состоит из четырёх компонентов,
построенных на wlroots 0.18:

- **ycomp** — композитор (~1700 строк)
- **yterm** — терминал (pty + VT100/ECMA-48, ~1000 строк)
- **ybar** — панель (layer-shell, ~500 строк)
- **ylaunch** — лаунчер (читает .desktop, ~450 строк)

Общая библиотека `libywt` (~1050 строк) инкапсулирует работу с
`wl_display`, `wl_shm`, `xdg_shell`, `wl_seat`, xkbcommon, шрифтами
(FreeType) и рисованием.

## Включение

    echo desktop > /etc/yaitso/session

При следующем логине `login` запустит `/usr/bin/start-desktop`, а он —
`ybar` и `ycomp`. `init` при `desktop` не запускает getty на tty1
(его занимает композитор), но поднимает getty на tty2–tty4 для
текстового fallback.

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
