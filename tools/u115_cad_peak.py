"""At 902.375 LongFast, count CAD 'detected' with no one sending, for several detPeak / symbol-count settings.
Run on the Cardputer Zero with the radio service stopped:  python3 u115_cad_peak.py /dev/serial0 runs"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq, cad_once  # noqa: E402

port, runs = sys.argv[1], int(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)
setup(s)
spi(s, 0x80, 0x00)
freq(s, 902.375)
# (symbol-count code, detPeak, detMin): code 2 = 4 symbols (Meshtastic), 3 = 8, 4 = 16
CASES = [(2, 24, 10), (2, 26, 10), (2, 28, 10), (2, 30, 10), (2, 32, 10), (3, 24, 10), (3, 26, 10), (4, 24, 10)]
for sym, peak, dmin in CASES:
    t = time.time()
    d = sum(1 for _ in range(runs) if cad_once(s, sym, peak, dmin))
    print("symbols code %d  detPeak %2d  detMin %2d   false detected %3d of %d  (%.1fs)"
          % (sym, peak, dmin, d, runs, time.time() - t), flush=True)
spi(s, 0x80, 0x00)
