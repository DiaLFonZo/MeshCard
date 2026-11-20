"""Read 32-bit registers from the running chip over raw SWD (no halt, no reset).
Usage: python peek.py ADDR [ADDR ...]   (hex)
       python peek.py --send COM6 ADDR ...  (first sends a PING frame on COM6)"""
import struct
import sys
import time

import libusb_package
import usb.core
import usb.util

args = sys.argv[1:]
if args and args[0] == "--send":
    import serial
    s = serial.Serial(args[1], 115200, timeout=0.1)
    s.write(bytes([0xA5, 1, 0, 0, 1]))
    time.sleep(0.05)
    args = args[2:]

dev = usb.core.find(idVendor=0x2E8A, idProduct=0x000C, backend=libusb_package.get_libusb1_backend())
usb.util.claim_interface(dev, 0)


def xfer(pkt):
    dev.write(0x04, bytes(pkt), 500)
    return bytes(dev.read(0x85, 512, 500))


def w(req, val):
    return bytes([req]) + struct.pack("<I", val)


def transfer(*reqs):
    return bytes([0x05, 0x00, len(reqs)]) + b"".join(reqs)


def recover():
    xfer(bytes([0x12, 72]) + b"\xff" * 7 + b"\x00\x00")
    xfer(transfer(bytes([0x02]), w(0x00, 0x1E), w(0x04, 0x54000000), w(0x08, 0), w(0x01, 0x23000002)))


xfer([0x02, 0x01])
xfer([0x11] + list(struct.pack("<I", 1_000_000)))
xfer([0x04, 0x00] + list(struct.pack("<HH", 64, 0)))
if args and args[0] == "--pc":   # sample PC/LR a few times (halt, read, resume)
    args = args[1:]
    for _ in range(5):
        for _t in range(10):
            try:
                recover()
                def ok(r):
                    if r[1] < 1 or r[2] != 1:
                        raise IOError
                    return r
                ok(xfer(transfer(w(0x05, 0xE000EDF0), w(0x0D, 0xA05F0003))))
                r = ok(xfer(transfer(w(0x05, 0xE000EDF0), bytes([0x0F]))))
                dh = struct.unpack("<I", r[3:7])[0]
                if not dh & (1 << 17):
                    raise IOError
                vals = []
                for reg in (15, 14, 0, 4):
                    ok(xfer(transfer(w(0x05, 0xE000EDF4), w(0x0D, reg))))
                    r = ok(xfer(transfer(w(0x05, 0xE000EDF8), bytes([0x0F]))))
                    vals.append(struct.unpack("<I", r[3:7])[0])
                ok(xfer(transfer(w(0x05, 0xE000EDF0), w(0x0D, 0xA05F0001))))
                print(f"PC=0x{vals[0]:08X} LR=0x{vals[1]:08X} r0=0x{vals[2]:08X} r4=0x{vals[3]:08X} DHCSR=0x{dh:08X}")
                break
            except Exception:
                pass
        time.sleep(0.1)
for a in args:
    addr = int(a, 16)
    for _ in range(10):
        try:
            recover()
            r = xfer(transfer(w(0x05, addr), bytes([0x0F])))
            if r[1] == 2 and r[2] == 1:
                print(f"0x{addr:08X} = 0x{struct.unpack('<I', r[3:7])[0]:08X}")
                break
        except Exception:
            pass
    else:
        print(f"0x{addr:08X} = read failed")
usb.util.release_interface(dev, 0)
