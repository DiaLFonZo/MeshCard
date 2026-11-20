"""CAD 'detected' count at 902.375 LongFast over time, with a recalibration halfway (receive only).
python3 u115_cad_time.py port batches"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq, cad_once  # noqa: E402

port, batches = sys.argv[1], int(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)
setup(s)
spi(s, 0x80, 0x00)
freq(s, 902.375)
t0 = time.time()
for b in range(batches):
    if b == batches // 2:
        spi(s, 0x80, 0x00)
        spi(s, 0x89, 0x7F)
        time.sleep(0.05)
        spi(s, 0x98, 0xE1, 0xE9)
        time.sleep(0.05)
        freq(s, 902.375)
        print("--- recalibrated ---", flush=True)
    d = sum(1 for _ in range(20) if cad_once(s, 2, 24, 10))
    print("t=%4.0fs  detected %2d of 20 %s" % (time.time() - t0, d, "#" * d), flush=True)
spi(s, 0x80, 0x00)
