"""Minimal FAT16 writer for an EFI System Partition image (no kernel fs support in sandbox)."""
import struct, sys, datetime

SECTOR = 512
TOTAL_SECTORS = 16384          # 8 MiB
RSVD = 1
NFATS = 2
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR  # 32
SPC = 1                        # 512-byte clusters -> enough clusters for FAT16

def fat_sectors():
    data = TOTAL_SECTORS - RSVD - ROOT_SECTORS
    # clusters C: C = (data - NFATS*S) / SPC ; FAT16 entries 2 bytes, S sectors per FAT
    s = 1
    while True:
        clusters = (TOTAL_SECTORS - RSVD - NFATS * s - ROOT_SECTORS) // SPC
        need = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
        if need <= s:
            return s, clusters
        s = need

FATSZ, NCLUS = fat_sectors()
ROOT_START = RSVD + NFATS * FATSZ
DATA_START = ROOT_START + ROOT_SECTORS

def fat_date(d):
    return ((d.year - 1980) << 9) | (d.month << 5) | d.day

class Fat16:
    def __init__(self):
        self.img = bytearray(TOTAL_SECTORS * SECTOR)
        self.fat = [0] * (NCLUS + 2)
        self.fat[0] = 0xFFF8
        self.fat[1] = 0xFFFF
        self.next_free = 2
        self.now = fat_date(datetime.date(2026, 10, 26))

    def _alloc(self, nbytes):
        n = max(1, (nbytes + SECTOR * SPC - 1) // (SECTOR * SPC))
        first = self.next_free
        if first + n - 1 > NCLUS + 1:
            raise SystemExit('image too small')
        for i in range(n):
            c = first + i
            self.fat[c] = (c + 1) if i < n - 1 else 0xFFFF
        self.next_free += n
        return first, n

    def _cluster_off(self, c):
        return (DATA_START + (c - 2) * SPC) * SECTOR

    def _write_data(self, first, data):
        off = self._cluster_off(first)
        self.img[off:off + len(data)] = data

    @staticmethod
    def _dirent(name83, attr, cluster, size, now):
        assert len(name83) == 11
        e = bytearray(32)
        e[0:11] = name83
        e[11] = attr
        struct.pack_into('<HHHHH', e, 14, 0, now, now, 0, 0)  # create time/date zero, write date
        struct.pack_into('<H', e, 22, 0)
        struct.pack_into('<H', e, 24, now)  # last access date
        struct.pack_into('<H', e, 26, cluster & 0xFFFF)
        struct.pack_into('<I', e, 28, size)
        return bytes(e)

    def add_file(self, name83, data):
        first, n = self._alloc(len(data)) if data else (0, 0)
        if data:
            self._write_data(first, data)
        return self._dirent(name83, 0x20, first, len(data), self.now)

    def add_dir(self, name83, entries_for_dir, parent_cluster):
        """entries_for_dir: list of 32-byte dirents to put inside (excluding . and ..)."""
        first, n = self._alloc(SECTOR * SPC)
        dot = self._dirent(b'.          ', 0x10, first, 0, self.now)
        dotdot = self._dirent(b'..         ', 0x10, parent_cluster, 0, self.now)
        body = dot + dotdot + b''.join(entries_for_dir)
        body = body.ljust(SECTOR * SPC, b'\x00')
        self._write_data(first, body)
        return self._dirent(name83, 0x10, first, 0, self.now)

    def finalize(self):
        # boot sector / BPB (FAT16)
        bs = bytearray(SECTOR)
        bs[0:3] = b'\xEB\x3C\x90'
        bs[3:11] = b'MSWIN4.1'
        struct.pack_into('<HBHBHHBHHHII', bs, 11, SECTOR, SPC, RSVD, NFATS, ROOT_ENTRIES,
                         TOTAL_SECTORS, 0xF8, FATSZ, 63, 255, 0, 0)
        bs[36] = 0x80; bs[38] = 0x29
        struct.pack_into('<I', bs, 39, 0x59415453)  # volume id
        bs[43:54] = b'YAITSO_EFI '
        bs[54:62] = b'FAT16   '
        bs[510] = 0x55; bs[511] = 0xAA
        self.img[0:SECTOR] = bs
        # FAT copies
        fat_bytes = bytearray(FATSZ * SECTOR)
        for i, v in enumerate(self.fat[:FATSZ * SECTOR // 2]):
            struct.pack_into('<H', fat_bytes, i * 2, v & 0xFFFF)
        for k in range(NFATS):
            off = (RSVD + k * FATSZ) * SECTOR
            self.img[off:off + len(fat_bytes)] = fat_bytes
        return bytes(self.img)

def build(efi_path, cfg_bytes, out_path):
    fs = Fat16()
    efi = open(efi_path, 'rb').read()
    # Pre-allocate directory clusters so '..' entries point at the real parent.
    efi_dir_cluster, _ = fs._alloc(SECTOR * SPC)
    boot_dir_cluster, _ = fs._alloc(SECTOR * SPC)
    file_entries = [fs.add_file(b'BOOTX64 EFI', efi), fs.add_file(b'GRUB    CFG', cfg_bytes)]
    boot_body = (fs._dirent(b'.          ', 0x10, boot_dir_cluster, 0, fs.now)
                 + fs._dirent(b'..         ', 0x10, efi_dir_cluster, 0, fs.now)
                 + b''.join(file_entries))
    fs._write_data(boot_dir_cluster, boot_body.ljust(SECTOR * SPC, b'\x00'))
    boot_entry = fs._dirent(b'BOOT       ', 0x10, boot_dir_cluster, 0, fs.now)
    efi_body = (fs._dirent(b'.          ', 0x10, efi_dir_cluster, 0, fs.now)
                + fs._dirent(b'..         ', 0x10, 0, 0, fs.now)
                + boot_entry)
    fs._write_data(efi_dir_cluster, efi_body.ljust(SECTOR * SPC, b'\x00'))
    root = bytearray(ROOT_SECTORS * SECTOR)
    lab = bytearray(32); lab[0:11] = b'YAITSO_EFI '; lab[11] = 0x08
    root[0:32] = lab
    root[32:64] = fs._dirent(b'EFI        ', 0x10, efi_dir_cluster, 0, fs.now)
    off = ROOT_START * SECTOR
    fs.img[off:off + len(root)] = root
    img = fs.finalize()
    open(out_path, 'wb').write(img)
    print('FAT16 image:', out_path, len(img), 'bytes; FATSZ', FATSZ, 'clusters', NCLUS, 'used', fs.next_free - 2)

if __name__ == '__main__':
    cfg = b"""# UEFI stub: locate the ISO volume that holds the real menu, then load it.
search --no-floppy --file --set=root /boot/grub/grub.cfg
if [ -z "$root" ]; then
    search --no-floppy --label --set=root YAITSO_OS
fi
set prefix=($root)/boot/grub
configfile $prefix/grub.cfg
"""
    build(sys.argv[1], cfg, sys.argv[2])
