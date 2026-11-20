"""Narrow-filter (7.8 kHz) RSSI scan to find a narrow signal (receive only). python3 u115_fine_scan.py port f0 f1 stepkHz"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq  # noqa: E402

port, f0, f1, st = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]) / 1000
s = serial.Serial(port, 115200, timeout=0.2)
setup(s)
spi(s, 0x80, 0x00)
spi(s, 0x8B, 0x0B, 0x00, 0x01, 0x01)   # BW 7.8 kHz for a sharp look
rows = []
f = f0
while f <= f1 + 1e-9:
    spi(s, 0x80, 0x00)
    freq(s, f)
    spi(s, 0x82, 0xFF, 0xFF, 0xFF)
    time.sleep(0.005)
    v = [-spi(s, 0x15, 0, 0)[2] / 2.0 for _ in range(8)]
    rows.append((f, sum(v) / len(v)))
    f += st
floor = sorted(r[1] for r in rows)[len(rows) // 2]
print("median %.1f dBm" % floor)
for f, a in rows:
    if a > floor + 4:
        print("%.4f MHz  %.1f dBm  (+%.1f)" % (f, a, a - floor))
spi(s, 0x80, 0x00)
