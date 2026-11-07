import struct, sys, hashlib
ISO = sys.argv[1]
d = open(ISO, 'rb').read()
SEC = 2048
def sec(n, c=1): return d[n*SEC:(n+c)*SEC]
pvd = sec(16)
assert pvd[0] == 1 and pvd[1:6] == b'CD001', 'PVD broken'
vol_sectors = struct.unpack_from('<I', pvd, 80)[0]
print('PVD volume size:', vol_sectors, 'sectors; file has', len(d)//SEC, '-> match' if vol_sectors == len(d)//SEC else '-> MISMATCH')
print('volume id:', pvd[40:72].decode().strip())

def read_dir(lba, length):
    data = d[lba*SEC: lba*SEC + length]
    out, i = [], 0
    while i < len(data):
        rlen = data[i]
        if rlen == 0:
            i = (i // SEC + 1) * SEC; continue
        rec = data[i:i+rlen]
        elba = struct.unpack_from('<I', rec, 2)[0]; elen = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]; nlen = rec[32]; nm = rec[33:33+nlen]
        if nm in (b'\x00', b'\x01'): nm = b'.' if nm == b'\x00' else b'..'
        sysuse = rec[33+nlen + (1 if nlen % 2 == 0 else 0):]
        j, rr = 0, None
        while j + 4 <= len(sysuse):
            ln = sysuse[j+2]
            if ln < 4: break
            if sysuse[j:j+2] == b'NM': rr = (rr or b'') + sysuse[j+5:j+ln]
            j += ln
        out.append(((rr or nm).decode('utf-8', 'replace'), elba, elen, bool(flags & 2)))
        i += rlen
    return out
root_lba = struct.unpack_from('<I', pvd, 156+2)[0]; root_len = struct.unpack_from('<I', pvd, 156+10)[0]
def tree(lba, ln, depth=0):
    for nm, elba, elen, isdir in read_dir(lba, ln):
        if nm in ('.', '..'): continue
        print('  '*depth + ('[D] ' if isdir else '    ') + nm + ('' if isdir else f'  ({elen} B @ lba {elba})'))
        if isdir: tree(elba, elen, depth+1)
tree(root_lba, root_len)

# El Torito
bc = sec(38)
print('\nEl Torito:')
bios = bc[32:64]
print('  BIOS default: boot=%#x load_rba=%d' % (bios[0], struct.unpack_from('<I', bios, 8)[0]))
ent = bc[96:128]
print('  UEFI section: platform=%#x load_rba=%d sectors=%d' % (bc[65], struct.unpack_from('<I', ent, 8)[0], struct.unpack_from('<H', ent, 6)[0]))
uefi_lba = struct.unpack_from('<I', ent, 8)[0]
img = sec(uefi_lba, struct.unpack_from('<H', ent, 6)[0] * 512 // SEC)
print('  UEFI image signature:', img[54:62], 'boot sig', img[510:512].hex())
import importlib.util
