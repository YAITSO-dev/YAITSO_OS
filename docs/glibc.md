# glibc в Yaitso OS

## Что это

Yaitso OS 0.3.1 добавляет слой совместимости с glibc для сторонних
prebuilt-приложений (Firefox, Chromium, Steam, VSCode, Discord).
Ядро системы (шелл, coreutils, GUI) продолжает работать на своём libc.

## Как работает

glibc устанавливается в изолированный префикс `/usr/glibc`:
