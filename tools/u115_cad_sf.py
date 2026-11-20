"""CAD 'detected' count at 902.375 MHz for several LoRa speeds (receive only). python3 u115_cad_sf.py port runs"""
import sys
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq, cad_once  # noqa: E402

port, runs = sys.argv[1], int(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)
setup(s)
for sf, bw, name in [(11, 0x05, "SF11 BW250 (LongFast)"), (7, 0x05, "SF7 BW250"), (9, 0x05, "SF9 BW250"),
                     (12, 0x05, "SF12 BW250"), (11, 0x04, "SF11 BW125"), (11, 0x06, "SF11 BW500"),
                     (11, 0x05, "SF11 BW250 again")]:
    spi(s, 0x80, 0x00)
    spi(s, 0x8B, sf, bw, 0x01, 0x01 if sf >= 12 else 0x00)
    freq(s, 902.375)
    d = sum(1 for _ in range(runs) if cad_once(s, 2, sf + 13, 10))
    print("%-22s detected %2d of %d" % (name, d, runs), flush=True)
spi(s, 0x80, 0x00)
