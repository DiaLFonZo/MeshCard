"""Do crystal trim writes stick? Write 0x20/0x20 in STDBY_RC then start RX and read back; then the same from STDBY_XOSC.
Receive only. Run on the Zero with the radio service stopped: python3 u115_trim_probe.py [/dev/serial0]"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import setup, spi  # noqa: E402
from u115_cad_test import freq  # noqa: E402

s = serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0", 115200, timeout=0.2)
setup(s)
freq(s, 902.375)


def rd():
    r = spi(s, 0x1D, 0x09, 0x11, 0, 0, 0)
    return "0x%02x/0x%02x" % (r[4], r[5])


def st():
    r = spi(s, 0xC0, 0)
    return ["?", "?", "STDBY_RC", "STDBY_XOSC", "FS", "RX", "TX", "?"][(r[1] >> 4) & 7]


spi(s, 0x80, 0x00); time.sleep(0.01)
print("start            %-10s trim %s" % (st(), rd()))
spi(s, 0x0D, 0x09, 0x11, 0x20, 0x20)
print("written in RC    %-10s trim %s" % (st(), rd()))
spi(s, 0x82, 0xFF, 0xFF, 0xFF); time.sleep(0.05)
print("after SetRx      %-10s trim %s" % (st(), rd()))
spi(s, 0x80, 0x00); time.sleep(0.01)
print("back to RC       %-10s trim %s" % (st(), rd()))
spi(s, 0x80, 0x01); time.sleep(0.02)
print("STDBY_XOSC       %-10s trim %s" % (st(), rd()))
spi(s, 0x0D, 0x09, 0x11, 0x20, 0x20)
print("written in XOSC  %-10s trim %s" % (st(), rd()))
spi(s, 0x82, 0xFF, 0xFF, 0xFF); time.sleep(0.05)
print("after SetRx      %-10s trim %s" % (st(), rd()))
spi(s, 0x80, 0x00); time.sleep(0.01)
print("back to RC       %-10s trim %s" % (st(), rd()))
spi(s, 0x80, 0x01); time.sleep(0.02)
print("XOSC again       %-10s trim %s" % (st(), rd()))
spi(s, 0x80, 0x00)
