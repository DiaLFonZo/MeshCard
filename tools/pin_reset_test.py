"""Run right after psoc4_acquire.py (chip halted in boot ROM).
Drives one pin low by hand and reports whether the whole chip restarted
(DHCSR S_RESET_ST) - to find a pin that cuts the board's own power.
Usage: python pin_reset_test.py PORT PIN"""
import struct
import sys
import time

import libusb_package
import usb.core
import usb.util

port, pin = int(sys.argv[1]), int(sys.argv[2])
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


def retry(fn, *a):
    for _ in range(20):
        try:
            return fn(*a)
        except Exception:
            try:
                recover()
            except Exception:
                pass
    raise IOError("link lost")


def _rd(a):
    r = xfer(transfer(w(0x05, a), bytes([0x0F])))
    if r[1] != 2 or r[2] != 1:
        raise IOError
    return struct.unpack("<I", r[3:7])[0]


def _wr(a, v):
    r = xfer(transfer(w(0x05, a), w(0x0D, v)))
    if r[1] != 2 or r[2] != 1:
        raise IOError


rd = lambda a: retry(_rd, a)
wr = lambda a, v: retry(_wr, a, v)

recover()
rd(0xE000EDF0)  # clears sticky S_RESET_ST
print("before: DHCSR=0x%08X TEST_MODE=0x%08X" % (rd(0xE000EDF0), rd(0x40030014)))
base = 0x40040000 + port * 0x100
hs = 0x40020000 + port * 0x100
wr(base, rd(base) & ~(1 << pin))                                   # DR low
wr(hs, rd(hs) & ~(0xF << (pin * 4)))                               # GPIO
wr(base + 8, (rd(base + 8) & ~(7 << (pin * 3))) | (6 << (pin * 3)))  # strong
time.sleep(0.05)
d = None
try:
    d = rd(0xE000EDF0)
except Exception as e:
    print("link lost after pulling pin low:", e)
if d is not None:
    print("low:    DHCSR=0x%08X  chip restarted=%s" % (d, bool(d & (1 << 25))))
    wr(base, rd(base) | (1 << pin))                                # release (high)
    time.sleep(0.05)
    try:
        d = rd(0xE000EDF0)
    except Exception as e:
        print("link lost after releasing pin:", e)
        d = None
if d is not None:
    print("after:  DHCSR=0x%08X  chip restarted=%s  TEST_MODE=0x%08X" % (d, bool(d & (1 << 25)), rd(0x40030014)))
usb.util.release_interface(dev, 0)
