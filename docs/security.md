# Yaitso OS 0.3.4 — безопасность

## HTTPS в yat

- OpenSSL `TLS_client_method`, минимум TLS 1.2
- Проверка цепочки по `/etc/ssl/certs/ca-certificates.crt`
- Проверка `Host:` через `X509_VERIFY_PARAM_set1_host` (SNI)
- Отключены SSLv2, SSLv3, TLSv1.0, TLSv1.1
- Таймауты 15 секунд на recv/send

## Подпись пакетов

- Ed25519, ключи в `/etc/yat/trusted.keys`
- `index.sig` — 64 байта подписи `index.txt`
- Отсутствие `.sig` или несовпадение — отказ установки

## WiFi

- Профили в `/etc/yaitso/wifi/*.conf`, каталог 0700
- PSK зашифрован XChaCha20-Poly1305
- Ключ в `/etc/yaitso/wifi/.key` (0600)

## IPC ycomp

- UNIX-сокет `$XDG_RUNTIME_DIR/ycomp.sock`, права 0600
- Проверка `SO_PEERCRED`: uid владельца или root
- Отказ в обслуживании чужих uid

## Login

- Lock после 3 неудачных попыток на 300 секунд
- Файлы блокировки в `/var/lib/yaitso/bans/`, режим 0600
- Автосброс при успешном входе
- `/etc/nologin` уважается для всех, кроме root
- Пароль проверяется по `/etc/shadow` (`getspnam` + `crypt`)
- Пустые и заблокированные (`!`, `*`) хеши отклоняются

## Смена пароля и пользователи

- `passwd` требует старый пароль для непривилегированных пользователей
- Обновление shadow атомарное: `mkstemp` + `fsync` + `rename`, режим 0600
- `sudo` сбрасывает дополнительные группы перед запуском команды
- `useradd`/`userdel` пишут passwd/group/shadow атомарно и проверяют имена
- `userdel` удаляет домашний каталог рекурсивно без вызова `system()`

## Шифрование диска

- Корневой раздел можно зашифровать LUKS2 (`--luks` в установщике)
- Шифр `aes-xts-plain64`, ключ 512 бит, хеш SHA-256
- `/boot` остаётся незашифрованным отдельным разделом: GRUB должен
  прочитать ядро и initramfs до запуска cryptsetup
- Пароль LUKS запрашивает initramfs при каждой загрузке
- `cryptsetup` и модули dm-crypt входят в загрузочный initramfs
- Без LUKS установка и разметка остаются прежними

## Известные ограничения

- Нет seccomp, AppArmor/SELinux
- Отпечатки сертификатов не пиннятся
- passwd/group/shadow — только через файлы (без SSSD)
