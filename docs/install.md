# Yaitso OS 0.3.4 — установка на диск

## Быстрый старт

1. Запишите ISO на USB (`dd if=yaitso-0.3.4.iso of=/dev/sdX bs=4M`) или
   загрузите его в QEMU.
2. В меню GRUB выберите **Yaitso OS 0.3.4 (install)**. Ядро получает
   параметр `yaitso.install=1`, и `init` запускает установщик на tty1
   вместо приглашения входа.
3. Ответьте на вопросы: диск, hostname, имя пользователя, пароль, тип
   сессии и шифрование LUKS. Для подтверждения уничтожения данных нужно
   ввести `YES`.

Если включить LUKS, установщик дополнительно попросит пароль для
зашифрованного корня. Этот пароль вводится при каждой загрузке.

Можно также загрузиться в обычную live-сессию и запустить установщик
вручную:

    yaitso-install

## Параметры командной строки

    yaitso-install --list
    yaitso-install --disk /dev/vda --hostname yaitso --user andrey \
        --password secret --gui --yes
    yaitso-install --disk /dev/vda --user andrey --password secret \
        --luks --luks-password 'disk-passphrase' --gui --yes

| Параметр | Значение |
|---|---|
| `--list` | показать диски и выйти |
| `--disk /dev/X` | целевой диск |
| `--hostname NAME` | имя компьютера (по умолчанию `yaitso`) |
| `--user NAME` | имя пользователя |
| `--password PASS` | пароль пользователя |
| `--gui`, `--no-gui` | графическая или текстовая сессия |
| `--efi`, `--uefi`, `--bios` | тип загрузки (по умолчанию автоопределение) |
| `--luks` | зашифровать корень LUKS2 |
| `--no-luks` | не шифровать (по умолчанию) |
| `--luks-password PASS` | пароль LUKS (иначе будет запрошен) |
| `--yes`, `-y` | без вопросов и подтверждения `YES` |
| `--force` | разрешить установку на диск со смонтированными разделами |

## Шифрование LUKS

С `--luks` (или после ответа «да» на вопрос установщика) корневой раздел
создаётся как контейнер LUKS2:

- шифр `aes-xts-plain64`, ключ 512 бит, хеш SHA-256;
- `/boot` выносится на отдельный незашифрованный раздел ext4 (1 GiB),
  потому что GRUB не умеет читать ядро и initramfs из LUKS;
- внутри контейнера — ext4 с меткой `yaitso-root`, он монтируется как
  `/dev/mapper/yaitso-root`;
- в `/etc/fstab` корень записан как `/dev/mapper/yaitso-root`, `/boot` —
  по UUID;
- GRUB передаёт ядру `root=/dev/mapper/yaitso-root cryptroot=/dev/...3
  bootroot=/dev/...2`.

Минимальный initramfs установленной системы содержит `cryptsetup` и модули
`dm-crypt`, `xts`, `aes`, `sha256`, `sha512`, `cbc`, `essiv`. При загрузке
он спрашивает пароль LUKS, открывает контейнер, монтирует `/boot` и делает
`switch_root`. Без LUKS разметка и загрузка остаются прежними.

## Что делает установщик

1. Проверяет, что устройство является блочным, имеет не менее 4 GiB и не
   смонтировано (без `--force`).
2. Создаёт таблицу GPT:
   - UEFI: раздел ESP (FAT32, 512 MiB) + корневой ext4;
   - BIOS: служебный раздел `bios_grub` + корневой ext4;
   - с LUKS: ESP/`bios_grub`, отдельный `/boot` ext4 (1 GiB) и корень
     внутри LUKS.
   Используются `parted`, при его отсутствии — `sfdisk`.
3. Форматирует разделы (`mkfs.vfat`, `mkfs.ext4`). С LUKS сначала
   выполняется `cryptsetup luksFormat`, затем `luksOpen`, и ext4 создаётся
   на `/dev/mapper/yaitso-root`.
4. Монтирует корень в `/mnt/yaitso` (с LUKS — `/boot` и ESP в
   `/boot/efi` для UEFI).
5. Копирует live-систему собственным кодом на C: сохраняются права,
   владельцы, символьные ссылки и время изменения.
6. Записывает `/etc/hostname`, `/etc/hosts`, `/etc/os-release`,
   `/etc/resolv.conf`, `/etc/yaitso/session` и `/etc/fstab` с UUID разделов.
7. Копирует `/boot/vmlinuz` и минимальный загрузочный
   `/boot/initramfs.gz`. Этот initramfs содержит busybox, драйверы
   накопителей и (при LUKS) `cryptsetup` с библиотеками; его `/init`
   открывает контейнер, монтирует корень и `/boot`, затем делает
   `switch_root` на установленную систему.
8. Создаёт пользователя, добавляет его в группы `wheel`, `video`,
   `input`, `render`, `audio`, задаёт SHA-512 хеш пароля и блокирует root.
   Группы нужны для доступа к DRM и устройствам ввода без udev.
9. Устанавливает GRUB:
   - UEFI: `grub-install --target=x86_64-efi`;
   - BIOS: `grub-install --target=i386-pc`;
   - ELF-бинарник `grub-install` запускается напрямую, shell-скрипт —
     через `bash.real`;
   - если установка GRUB не удалась, UEFI-система получает резервный
     `/boot/efi/EFI/BOOT/BOOTX64.EFI`, который ищет установленный
     `grub.cfg` (и в `/grub/`, и в `/boot/grub/`).
10. Удаляет `/etc/yaitso/autologin` (автовход остаётся только в live-режиме)
    и предлагает перезагрузку.

## Требования к live-образу

В live-системе должны быть:

- `/usr/bin/yaitso-install` — установщик;
- `parted`/`sfdisk`, `mkfs.ext4`, `mkfs.vfat`, `blkid`;
- `cryptsetup` — для установки с LUKS;
- `grub-install`, `/usr/lib/grub`, `/usr/share/grub`;
- `/usr/bin/bash.real` — для запуска `grub-install`, если он оформлен
  shell-скриптом (ELF-бинарник запускается напрямую);
- `/boot/vmlinuz` и `/boot/initramfs.gz`.

Всё это `build/mkiso.sh` копирует автоматически вместе с библиотеками,
включая `libcrypt.so.1` для `getty`, `login`, `passwd`, `su` и `sudo`.

## После установки

При загрузке появится `login`. Введите созданного пользователя и пароль —
`login` запустит графическую сессию (`ycomp` + `ybar` + `yterm`), если при
установке была выбрана графическая среда. Выход из сессии возвращает к
приглашению входа.
