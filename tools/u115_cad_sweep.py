"""CAD 'detected' count per frequency around a centre (receive only). python3 u115_cad_sweep.py port centre span step runs"""
import sys
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq, cad_once  # noqa: E402

port, c, span, step, runs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
s = serial.Serial(port, 115200, timeout=0.2)
setup(s)
f = c - span
while f <= c + span + 1e-6:
    spi(s, 0x80, 0x00)
    freq(s, f)
    d = sum(1 for _ in range(runs) if cad_once(s, 2, 24, 10))
    print("%.3f MHz : %2d of %d %s" % (f, d, runs, "#" * d), flush=True)
    f += step
spi(s, 0x80, 0x00)
