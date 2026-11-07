import os, stat, gzip, sys
src, out = sys.argv[1], sys.argv[2]
buf = bytearray()
ino = 1
def add(name, st, data=b''):
    global ino
    namebytes = name.encode() + b'\x00'
    hdr = b'070701' + b''.join(('%08X' % v).encode() for v in [
        ino, st.st_mode, 0, 0, st.st_nlink if not stat.S_ISDIR(st.st_mode) else 2,
        0, len(data), 0, 0, 0, 0, len(namebytes), 0])
    ino += 1
    buf.extend(hdr); buf.extend(namebytes)
    while len(buf) % 4: buf.append(0)
    buf.extend(data)
    while len(buf) % 4: buf.append(0)
# root '.' first, then walk sorted for determinism
add('.', os.lstat(src))
for root, dirs, files in os.walk(src):
    dirs.sort(); files.sort()
    for n in dirs + files:
        p = os.path.join(root, n)
        rel = os.path.relpath(p, src)
        st = os.lstat(p)
        if stat.S_ISLNK(st.st_mode):
            add(rel, st, os.readlink(p).encode())
        elif stat.S_ISDIR(st.st_mode):
            add(rel, st)
        elif stat.S_ISREG(st.st_mode):
            add(rel, st, open(p, 'rb').read())
# TRAILER
trailer_st = os.lstat(src)
namebytes = b'TRAILER!!!\x00'
hdr = b'070701' + b''.join(('%08X' % v).encode() for v in [0,0,0,1,0,0,0,0,0,0,0,len(namebytes),0])
buf.extend(hdr + namebytes)
while len(buf) % 512: buf.append(0)
with gzip.open(out, 'wb', compresslevel=9) as f:
    f.write(bytes(buf))
print('packed', out, 'uncompressed', len(buf))
