"""Count how often the U115 radio's channel check (CAD) says "LoRa detected" on a quiet channel (receive only).
Run on the Cardputer Zero with the radio service stopped:  python3 u115_cad_test.py [/dev/serial0] [runs]
Tries a few frequencies and detection thresholds, LongFast modulation (SF11, BW250)."""
import struct
import sys
import time

sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import call, spi, setup  # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
RUNS = int(sys.argv[2]) if len(sys.argv) > 2 and __name__ == "__main__" else 50


def freq(s, mhz):
    v = int(round(mhz * 1e6 * (1 << 25) / 32e6))
    spi(s, 0x86, *struct.pack(">I", v))


def irq(s):
    r = spi(s, 0x12, 0, 0, 0)
    return (r[2] << 8) | r[3]


def cad_once(s, sym, peak, mn):
    spi(s, 0x80, 0x00)
    spi(s, 0x08, 0x01, 0x80, 0x01, 0x80, 0, 0, 0, 0)       # IRQ: CAD_DONE | CAD_DETECTED, both on DIO1
    spi(s, 0x02, 0x43, 0xFF)                               # clear all
    spi(s, 0x88, sym, peak, mn, 0x00, 0, 0, 0)             # CAD params, exit to standby
    spi(s, 0xC5)                                           # SetCAD
    end = time.time() + 0.5
    while time.time() < end:
        v = irq(s)
        if v & 0x80:
            return bool(v & 0x100)
    return None


import serial  # noqa: E402

CASES = [(902.375, 2, 24, 10), (925.0, 2, 24, 10), (902.375, 2, 26, 10), (902.375, 2, 28, 10), (902.375, 3, 24, 10)]
if __name__ != "__main__":
    CASES = []  # imported for its helpers only
else:
    s = serial.Serial(PORT, 115200, timeout=0.2)
    setup(s)
for mhz, sym, peak, mn in CASES:
    spi(s, 0x80, 0x00)
    freq(s, mhz)
    det = none = 0
    for _ in range(RUNS):
        r = cad_once(s, sym, peak, mn)
        if r is None:
            none += 1
        elif r:
            det += 1
    print("%.3f MHz  symbols code %d  peak %d  min %d : detected %d of %d (no answer %d)"
          % (mhz, sym, peak, mn, det, RUNS, none), flush=True)
if CASES:
    spi(s, 0x80, 0x00)
