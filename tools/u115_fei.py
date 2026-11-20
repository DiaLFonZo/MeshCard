"""Listen on 902.375 LongFast and, for each packet heard, print the U115's frequency error against the sender,
SNR and RSSI (receive only). Also prints the crystal trim registers 0x0911/0x0912.
Run on the Cardputer Zero with the radio service stopped:  python3 u115_fei.py [/dev/serial0] [seconds] [xta xtb]"""
import sys
import time

import serial

sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq, irq  # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 90
TRIM = [int(x, 0) for x in sys.argv[3:5]] if len(sys.argv) > 4 else None
# "sweep" instead of a trim pair: after each packet heard, step XTA=XTB through these values
SWEEP = [0x12, 0x16, 0x1A, 0x1E, 0x22, 0x26, 0x2A, 0x2E, 0x0A] if len(sys.argv) > 3 and sys.argv[3] == "sweep" else None
BW_KHZ = 250.0

s = serial.Serial(PORT, 115200, timeout=0.2)
setup(s)
spi(s, 0x80, 0x00)
spi(s, 0x80, 0x01)  # STDBY_XOSC: trim writes only stick with the oscillator running
time.sleep(0.02)
if TRIM:
    spi(s, 0x0D, 0x09, 0x11, TRIM[0], TRIM[1])
r = spi(s, 0x1D, 0x09, 0x11, 0x00, 0x00, 0x00)
print("XTA trim 0x%02x  XTB trim 0x%02x" % (r[4], r[5]), flush=True)
freq(s, 902.375)
spi(s, 0x08, 0x02, 0x62, 0x02, 0x62, 0, 0, 0, 0)   # RX_DONE | HDR_ERR | CRC_ERR | TIMEOUT
spi(s, 0x02, 0x43, 0xFF)
spi(s, 0x82, 0xFF, 0xFF, 0xFF)
end = time.time() + SECS
n = 0
while time.time() < end:
    v = irq(s)
    if not (v & 0x262):
        time.sleep(0.02)
        continue
    st = spi(s, 0x14, 0, 0, 0, 0)
    fe = spi(s, 0x1D, 0x07, 0x6B, 0x00, 0x00, 0x00, 0x00)
    efe = ((fe[4] << 16) | (fe[5] << 8) | fe[6]) & 0x0FFFFF
    if efe & 0x80000:
        efe -= 0x100000
    err_hz = efe * 1.55 * (BW_KHZ / 1600.0)
    bs = spi(s, 0x13, 0, 0, 0)
    kind = "ok" if v & 0x02 else ("crc" if v & 0x40 else "hdr/timeout")
    n += 1
    print("%s  len=%3d  rssi=%6.1f  snr=%5.2f  freq error %+7.0f Hz (%+.1f ppm)"
          % (kind, bs[2], -st[2] / 2.0, (st[3] if st[3] < 128 else st[3] - 256) / 4.0, err_hz, err_hz / 902.375),
          flush=True)
    spi(s, 0x02, 0x43, 0xFF)
    if SWEEP and n < len(SWEEP):
        spi(s, 0x80, 0x01)  # STDBY_XOSC, see above
        time.sleep(0.02)
        spi(s, 0x0D, 0x09, 0x11, SWEEP[n], SWEEP[n])
        r = spi(s, 0x1D, 0x09, 0x11, 0x00, 0x00, 0x00)
        print("  -> trim now 0x%02x/0x%02x" % (r[4], r[5]), flush=True)
        freq(s, 902.375)
    spi(s, 0x82, 0xFF, 0xFF, 0xFF)
print("heard %d packets" % n)
spi(s, 0x80, 0x00)
