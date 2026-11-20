"""Save the Zero's LCD (/dev/fb0, RGB565 320x170) as a PNG, for checking what is on screen.
Usage: python3 fb_snap.py out.png"""
import struct
import sys
import zlib

W, H, STRIDE = 320, 170, 640
d = open("/dev/fb0", "rb").read(STRIDE * H)
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        p = d[y * STRIDE + 2 * x] | d[y * STRIDE + 2 * x + 1] << 8
        raw += bytes(((p >> 11) << 3, ((p >> 5) & 63) << 2, (p & 31) << 3))


def chunk(t, b):
    return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
