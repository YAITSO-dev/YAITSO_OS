import struct, sys, os
SRC = '/mnt/user-data/uploads/yaitso-0_3_1.iso'
OUT = sys.argv[1]
NEW_INITRD = '/tmp/build/initramfs_v3.gz'
NEW_EFI = '/tmp/build/efi.img'
SEC = 2048

iso = bytearray(open(SRC, 'rb').read())
orig_sectors = len(iso) // SEC

def pad_to_sector(buf):
    while len(buf) % SEC: buf.append(0)

def both_le_be_32(buf, off, val):
    struct.pack_into('<I', buf, off, val)
    struct.pack_into('>I', buf, off + 4, val)

def find_record(dir_lba, dir_len, name):
    """Return absolute byte offset of the directory record for `name` inside a directory extent."""
    base = dir_lba * SEC
    pos = 0
    while pos < dir_len:
        off = base + (pos % SEC if False else pos)
        rlen = iso[base + pos]
        if rlen == 0:
            pos = ((pos // SEC) + 1) * SEC
            continue
        nlen = iso[base + pos + 32]
        nm = bytes(iso[base + pos + 33: base + pos + 33 + nlen])
        if nm not in (b'\x00', b'\x01'):
            # Rock Ridge alt name
            sysuse = bytes(iso[base + pos + 33 + nlen + (1 if nlen % 2 == 0 else 0): base + pos + rlen])
            j, rr = 0, None
            while j + 4 <= len(sysuse):
                ln = sysuse[j+2]
                if ln < 4: break
                if sysuse[j:j+2] == b'NM': rr = (rr or b'') + sysuse[j+5:j+ln]
                j += ln
            shown = rr.decode('utf-8', 'replace') if rr else nm.decode('ascii', 'replace').split(';')[0]
            if shown == name:
                return base + pos
        pos += rlen
    raise SystemExit('record not found: ' + name)

def dir_extent(parent_lba, parent_len, name):
    off = find_record(parent_lba, parent_len, name)
    lba = struct.unpack_from('<I', iso, off + 2)[0]
    ln = struct.unpack_from('<I', iso, off + 10)[0]
    return off, lba, ln

pvd = 16 * SEC
root_lba = struct.unpack_from('<I', iso, pvd + 156 + 2)[0]
root_len = struct.unpack_from('<I', iso, pvd + 156 + 10)[0]

# 1) append new initramfs and new EFI partition image (both sector-aligned)
initrd = bytearray(open(NEW_INITRD, 'rb').read())
efi = bytearray(open(NEW_EFI, 'rb').read())
pad_to_sector(iso)
initrd_lba = len(iso) // SEC
iso += initrd; pad_to_sector(iso)
efi_lba = len(iso) // SEC
iso += efi; pad_to_sector(iso)
total = len(iso) // SEC

# 2) point boot/initramfs.gz at the new copy
_, boot_lba, boot_len = dir_extent(root_lba, root_len, 'boot')
rec = find_record(boot_lba, boot_len, 'initramfs.gz')
both_le_be_32(iso, rec + 2, initrd_lba)
both_le_be_32(iso, rec + 10, len(initrd))

# 3) El Torito: UEFI section entry -> FAT EFI System Partition image
bc = 38 * SEC
assert iso[bc + 64] == 0x91 and iso[bc + 65] == 0xEF, 'unexpected boot catalog layout'
ent = bc + 96
assert iso[ent] == 0x88 or iso[ent] == 0x00
struct.pack_into('<H', iso, ent + 6, len(efi) // 512)   # sector count (512-byte units)
struct.pack_into('<I', iso, ent + 8, efi_lba)           # load RBA

# 4) Volume space size in the PVD (both endians)
both_le_be_32(iso, pvd + 80, total)

open(OUT, 'wb').write(iso)
print('wrote', OUT, len(iso), 'bytes;', 'initramfs@', initrd_lba, 'efi@', efi_lba, 'total sectors', total, '(orig', orig_sectors, ')')
