"""Does the chip's serial output reach the PC? Takes over P3.1 (UART TX) as a
plain pin over SWD, pulls it low/high many times, and counts what arrives on
the PC serial port (a low line shows up as 0x00 bytes / garbage).
Then pins P3.0 the same way to cover swapped wires.
Usage: python tx_wire_test.py COM6   (reset the chip afterwards)"""
import struct
import sys
import time

import libusb_package
import serial
import usb.core
import usb.util

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


def _rd(a):
    r = xfer(transfer(w(0x05, a), bytes([0x0F])))
    if r[1] != 2 or r[2] != 1:
        raise IOError
    return struct.unpack("<I", r[3:7])[0]


def _wr(a, v):
    r = xfer(transfer(w(0x05, a), w(0x0D, v)))
    if r[1] != 2 or r[2] != 1:
        raise IOError


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


def rd(a):
    return retry(_rd, a)


def wr(a, v):
    retry(_wr, a, v)


xfer([0x02, 0x01]); xfer([0x11] + list(struct.pack("<I", 1_000_000))); xfer([0x04, 0x00] + list(struct.pack("<HH", 64, 0)))
recover()
wr(0xE000EDF0, 0xA05F0003)  # halt so firmware doesn't fight us
s = serial.Serial(sys.argv[1], 115200, timeout=0.1)
for pin in (1, 0):
    hs = rd(0x40020300); pc = rd(0x40040308)
    wr(0x40020300, hs & ~(0xF << (pin * 4)))                       # plain GPIO
    wr(0x40040308, (pc & ~(7 << (pin * 3))) | (6 << (pin * 3)))   # strong output
    s.reset_input_buffer()
    for _ in range(50):
        wr(0x40040300, rd(0x40040300) & ~(1 << pin)); time.sleep(0.002)
        wr(0x40040300, rd(0x40040300) | (1 << pin)); time.sleep(0.002)
    time.sleep(0.2)
    got = s.read(4096)
    print(f"P3.{pin} toggled -> PC received {len(got)} bytes {got[:16].hex(' ')}")
usb.util.release_interface(dev, 0)
