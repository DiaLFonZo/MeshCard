"""At 902.375 LongFast, count CAD 'detected' under different U115 set-ups (receive only).
python3 u115_cad_variants.py port runs"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi, call  # noqa: E402
from u115_cad_test import freq, cad_once, irq  # noqa: E402

port, runs = sys.argv[1], int(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)


def quiet_cad(s):
    """CAD, then wait silently on the cable for the DIO1 event before asking the result."""
    spi(s, 0x80, 0x00)
    spi(s, 0x08, 0x01, 0x80, 0x01, 0x80, 0, 0, 0, 0)
    spi(s, 0x02, 0x43, 0xFF)
    spi(s, 0x88, 2, 24, 10, 0x00, 0, 0, 0)
    s.reset_input_buffer()
    spi(s, 0xC5)
    time.sleep(0.08)  # 4 symbols at SF11/BW250 is ~33 ms; no cable traffic meanwhile
    v = irq(s)
    return bool(v & 0x100)


def count(fn):
    return sum(1 for _ in range(runs) if fn())


def base():
    setup(s); spi(s, 0x80, 0x00); freq(s, 902.375)


cases = []
base(); cases.append(("as Meshtastic (boosted gain, switch power on, DIO2 switch)", count(lambda: cad_once(s, 2, 24, 10))))
base(); spi(s, 0x0D, 0x08, 0xAC, 0x94)
cases.append(("normal RX gain instead of boosted", count(lambda: cad_once(s, 2, 24, 10))))
base(); cases.append(("cable silent during the check", count(lambda: quiet_cad(s))))
base(); spi(s, 0x9D, 0x00)
cases.append(("DIO2 not driving the antenna switch", count(lambda: cad_once(s, 2, 24, 10))))
base(); call(s, 0x05, b"\x00")
cases.append(("antenna-switch power (P6.1) off", count(lambda: cad_once(s, 2, 24, 10))))
call(s, 0x05, b"\x01")
base(); cases.append(("as Meshtastic again", count(lambda: cad_once(s, 2, 24, 10))))
for name, d in cases:
    print("%-58s detected %3d of %d" % (name, d, runs), flush=True)
spi(s, 0x80, 0x00)
