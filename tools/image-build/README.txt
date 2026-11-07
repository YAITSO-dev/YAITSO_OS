Сборка образа без grub-mkrescue/xorriso (в песочнице их не было):
1. Собрать бинарники из исходников (Makefile в каждой папке; шелл: gcc src/*.c -DYAITSO_VERSION=\"0.3.1\").
2. Собрать корень: базовый initramfs + бинарники + distro/airootfs/ (копируется поверх корня).
3. pack_initramfs.py <корень> initramfs.gz           — newc cpio + gzip
4. mkfat16.py BOOTX64.EFI efi.img                     — FAT16 EFI System Partition (для UEFI El Torito)
5. build_iso_v3.py <вывод.iso>                        — дописывает initramfs и efi.img в исходный ISO
                                                        и переключает запись UEFI; BIOS-часть не меняется
6. verify_iso.py <iso>                                — проверка структуры ISO и записей загрузки
Пути в скриптах захардкожены под эту сборку: поправьте их под свою.
Графика собирается отдельно: ./build-gui.sh (нужен wlroots 0.18), результат в gui-out/usr/bin.
