"""Try the SX1262 in TCXO mode (DIO3 powers the oscillator) with a LONG start-up wait, each voltage, and report
the chip's error flags; then the same in crystal mode. Receive only, no transmit.
Run on the Cardputer Zero with the radio service stopped: python3 u115_tcxo_probe2.py [/dev/serial0]"""
import sys
import time
import serial
sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import call, spi  # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
s = serial.Serial(PORT, 115200, timeout=0.2)
time.sleep(0.2)
s.reset_input_buffer()


def errors():
    r = spi(s, 0x17, 0, 0, 0)
    e = (r[2] << 8) | r[3]
    names = ["RC64K_CALIB", "RC13M_CALIB", "PLL_CALIB", "ADC_CALIB", "IMG_CALIB", "XOSC_START", "PLL_LOCK", "?", "PA_RAMP"]
    return "0x%03x %s" % (e, "|".join(n for k, n in enumerate(names) if e >> k & 1))


def status():
    r = spi(s, 0xC0, 0)
    return "mode=%d cmd=%d" % ((r[1] >> 4) & 7, (r[1] >> 1) & 7)


for volt in [None, 0, 1, 2, 3, 4, 5, 6, 7]:
    call(s, 0x03)                      # hard reset
    time.sleep(0.05)
    spi(s, 0x80, 0x00)
    spi(s, 0x07, 0, 0)                 # clear errors
    if volt is not None:
        delay = int(100000 / 15.625)   # 100 ms start-up wait
        spi(s, 0x97, volt, (delay >> 16) & 0xFF, (delay >> 8) & 0xFF, delay & 0xFF)
    spi(s, 0x89, 0x7F)                 # calibrate all (starts XOSC)
    time.sleep(0.3)
    spi(s, 0x80, 0x01)                 # STDBY_XOSC: needs a working clock
    time.sleep(0.2)
    label = "crystal mode" if volt is None else "TCXO mode, voltage code %d" % volt
    print("%-28s %-16s errors %s" % (label, status(), errors()), flush=True)
call(s, 0x03)
