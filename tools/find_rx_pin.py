"""Find which chip pin the PC's serial TX really reaches: stream 0x00 bytes on
the serial port (line mostly low) and sample every GPIO port's input register
over raw SWD. Pins that read 0 only while streaming are the receive line.
Usage: python find_rx_pin.py COM6"""
import struct
import sys
import threading
import time

import libusb_package
import serial
import usb.core
import usb.util

PORTS = range(0, 7)
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


def sample():
    out = {}
    for p in PORTS:
        r = xfer(transfer(w(0x05, 0x40040004 + p * 0x100), bytes([0x0F])))
        if r[1] == 2 and r[2] == 1:
            out[p] = struct.unpack("<I", r[3:7])[0] & 0xFF
    return out


def collect(n):
    lows = {p: 0 for p in PORTS}
    highs = {p: 0 for p in PORTS}
    for _ in range(n):
        try:
            s = sample()
        except Exception:
            recover()
            continue
        for p, v in s.items():
            lows[p] |= (~v) & 0xFF
            highs[p] |= v
    return lows, highs


xfer([0x02, 0x01]); xfer([0x11] + list(struct.pack("<I", 1_000_000))); xfer([0x04, 0x00] + list(struct.pack("<HH", 64, 0)))
recover()
# Unused pins (drive mode 0) have their input buffer off and always read 0.
# Switch every drive-mode-0 pin to digital input (mode 1): harmless, it only
# listens. Pins already configured keep their mode.
for p in PORTS:
    a = 0x40040008 + p * 0x100
    for _ in range(10):
        try:
            r = xfer(transfer(w(0x05, a), bytes([0x0F])))
            pc = struct.unpack("<I", r[3:7])[0]
            for i in range(8):
                if (pc >> (3 * i)) & 7 == 0:
                    pc |= 1 << (3 * i)
            xfer(transfer(w(0x05, a), w(0x0D, pc)))
            break
        except Exception:
            recover()
if "--p31-input" in sys.argv:
    # swap test: make the chip's UART TX pin (P3.1) a plain input
    for a, mask, val in ((0x40020300, 0xF0, 0x00), (0x40040308, 0x38, 0x08)):
        r = xfer(transfer(w(0x05, a), bytes([0x0F])))
        v = (struct.unpack("<I", r[3:7])[0] & ~mask) | val
        xfer(transfer(w(0x05, a), w(0x0D, v)))
idle_lows, idle_highs = collect(300)

ser = serial.Serial(sys.argv[1], 115200, timeout=0.1)
stop = False


def streamer():
    while not stop:
        ser.write(b"\x00" * 64)


th = threading.Thread(target=streamer); th.start()
time.sleep(0.1)
busy_lows, busy_highs = collect(300)
stop = True; th.join()

for p in PORTS:
    newly_low = busy_lows[p] & ~idle_lows[p] & 0xFF
    toggling = busy_lows[p] & busy_highs[p] & ~(idle_lows[p] & idle_highs[p]) & 0xFF
    if newly_low or toggling:
        print(f"port {p}: pins that go low only while PC sends: {[i for i in range(8) if newly_low >> i & 1]}"
              f"  toggling: {[i for i in range(8) if toggling >> i & 1]}")
print("idle input levels:", {p: f"{idle_highs[p]:08b}" for p in PORTS})
usb.util.release_interface(dev, 0)
