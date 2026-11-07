import struct, sys
ISO, OUT = sys.argv[1], sys.argv[2]
d = open(ISO, 'rb').read()
SEC = 2048
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
        sysuse = rec[33+nlen + (1 if nlen % 2 == 0 else 0):]
        j, rr = 0, None
        while j + 4 <= len(sysuse):
            ln = sysuse[j+2]
            if ln < 4: break
            if sysuse[j:j+2] == b'NM': rr = (rr or b'') + sysuse[j+5:j+ln]
            j += ln
        name = (rr or nm).decode('utf-8', 'replace')
        if name not in ('.', '..') and nm not in (b'\x00', b'\x01'):
            out.append((name, elba, elen, bool(flags & 2)))
        i += rlen
    return out
pvd = d[16*SEC:17*SEC]
root_lba = struct.unpack_from('<I', pvd, 158)[0]; root_len = struct.unpack_from('<I', pvd, 166)[0]
def find(lba, ln, name):
    for nm, elba, elen, isdir in read_dir(lba, ln):
        if nm == name: return elba, elen
    return None
boot = find(root_lba, root_len, 'boot')
blob_loc = find(boot[0], boot[1], 'initramfs.gz')
print('initramfs.gz in ISO: lba', blob_loc[0], 'len', blob_loc[1])
blob = d[blob_loc[0]*SEC: blob_loc[0]*SEC + blob_loc[1]]
open(OUT, 'wb').write(blob)
