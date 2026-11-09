#!/usr/bin/env python3
# shots.py WAD DIR_A DIR_B OUTDIR: compares the SN.RAW frame dumps of two runs
# (320x240 palette indices) and writes A, B and a difference image as PNGs.
import sys, os, struct, zlib, glob
wad, da, db, out = sys.argv[1:5]
os.makedirs(out, exist_ok=True)
w = open(wad, 'rb').read()
n, ofs = struct.unpack('<ii', w[4:12])
pal = None
for i in range(n):
    p, sz, name = struct.unpack('<ii8s', w[ofs + 16 * i:ofs + 16 * i + 16])
    if name.rstrip(b'\0') == b'PLAYPAL':
        pal = w[p:p + 768]
def png(path, rgb, W=320, H=240):
    raw = b''.join(b'\0' + rgb[y * W * 3:(y + 1) * W * 3] for y in range(H))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
def rgb(buf):
    return b''.join(pal[c * 3:c * 3 + 3] for c in buf)
for fa in sorted(glob.glob(os.path.join(da, 'S*.RAW')), key=lambda f: int(os.path.basename(f)[1:-4])):
    name = os.path.basename(fa)
    fb = os.path.join(db, name)
    if not os.path.exists(fb):
        print(name, 'missing in B'); continue
    a = open(fa, 'rb').read(); b = open(fb, 'rb').read()
    diff = sum(1 for x, y in zip(a, b) if x != y)
    print('%-10s %6d of %d pixels differ (%.2f%%)' % (name, diff, len(a), 100.0 * diff / len(a)))
    t = name[:-4]
    png(os.path.join(out, t + '_a.png'), rgb(a))
    png(os.path.join(out, t + '_b.png'), rgb(b))
    d = bytearray()
    for x, y in zip(a, b):
        d += b'\xff\x00\x00' if x != y else bytes(c // 3 for c in pal[x * 3:x * 3 + 3])
    png(os.path.join(out, t + '_diff.png'), bytes(d))
