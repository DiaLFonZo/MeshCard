"""Transmit test rig: the U115 sends long LoRa test packets (LongFast, 902.375 MHz, 22 dBm) under several chip
set-ups, so a listening Heltec (USB log) can report each one as received or CRC-broken.
Packets carry a Meshtastic-shaped header: to=broadcast, hop limit 0 (nobody relays), channel hash 0xFF (nobody
decodes), id = 0xA0000000 | config<<8 | n, so each Heltec log line maps back to its set-up.
Run on the Cardputer Zero with the radio service stopped:  python3 u115_tx_test.py [/dev/serial0] [packets per config]"""
import random
import struct
import sys
import time

import serial

sys.path.insert(0, "/tmp")
from u115_rssi_scan_lib import call, spi, setup  # noqa: E402
from u115_cad_test import freq, irq  # noqa: E402

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 5
FROM = 0x7E570001

CONFIGS = [
    # name, total length, regulator (1=DC-DC), trim (None = chip default), xosc before TX, ramp code,
    # coding rate (1 = 4/5 as LongFast, 4 = 4/8), heat (a 144 B dummy send right before), frequency MHz,
    # preamble symbols (Meshtastic sends 16)
    # 2026-11-20 weak unit (Heltec counts): 144 B with preamble 16/160/370 0 of 4 each, 610 (5 s) 4 of 4,
    # heat + 16 3 of 4, heat + 160 3 of 4, 30 B 4 of 4
    # 4 s 3/7, 5 s 7/7, 237 B 5 s 6/7. Now: normal preamble, run with U115_TCXO=2 (maker's 1.8 V clock power)
    # Now: where the warm-up can start - a short direct message is 59 B on air (43 B encrypted + 16 B header)
    ("59 B preamble 16",               59, 1, None, False, 0x04, 1, False, 902.375, 16),
    ("75 B preamble 16",               75, 1, None, False, 0x04, 1, False, 902.375, 16),
    ("90 B preamble 16",               90, 1, None, False, 0x04, 1, False, 902.375, 16),
]
GAP = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

s = serial.Serial(PORT, 115200, timeout=0.2)
setup(s)                                   # reset + LongFast modem, sync word, image cal


def reg_rmw(addr, clear, set_):
    r = spi(s, 0x1D, addr >> 8, addr & 0xFF, 0, 0)
    v = (r[4] & ~clear) | set_
    spi(s, 0x0D, addr >> 8, addr & 0xFF, v)


def tx_setup(regulator, ramp):
    spi(s, 0x80, 0x00)
    spi(s, 0x96, regulator)
    spi(s, 0x95, 0x04, 0x07, 0x00, 0x01)   # PA config for +22 dBm (SX1262 table)
    spi(s, 0x8E, 22, ramp)                 # +22 dBm
    spi(s, 0x0D, 0x08, 0xE7, 0x38)         # OCP 140 mA
    reg_rmw(0x08D8, 0x00, 0x1E)            # PA clamping fix
    reg_rmw(0x0889, 0x00, 0x04)            # modulation quality fix (BW != 500)
    spi(s, 0x8F, 0x00, 0x00)
    freq(s, 902.375)
    spi(s, 0x08, 0x02, 0x01, 0x02, 0x01, 0, 0, 0, 0)   # TX_DONE | TIMEOUT


def send(cfg_index, k, length, trim, xosc, cr=1, heat=False, mhz=902.375, pre=16):
    spi(s, 0x80, 0x00)
    if heat:
        spi(s, 0x8B, 0x0B, 0x05, 0x01, 0x00)
        spi(s, 0x8C, 0x00, 0x10, 0x00, 144, 0x01, 0x00)
        spi(s, 0x0E, 0x00, *bytes(144))
        spi(s, 0x02, 0x43, 0xFF)
        spi(s, 0x83, 0x00, 0x00, 0x00)
        t1 = time.time()
        while time.time() - t1 < 6 and not (irq(s) & 0x201):
            time.sleep(0.02)
    spi(s, 0x8B, 0x0B, 0x05, cr, 0x00)
    freq(s, mhz)
    pid = 0xA0000000 | (cfg_index << 8) | k
    rnd = random.Random(pid)
    body = bytes(rnd.getrandbits(8) for _ in range(length - 16))
    hdr = struct.pack("<III", 0xFFFFFFFF, FROM, pid) + bytes([0x00, 0xFF, 0x00, 0x00])
    pkt = hdr + body
    spi(s, 0x80, 0x00)
    spi(s, 0x8C, pre >> 8, pre & 0xFF, 0x00, len(pkt), 0x01, 0x00)
    spi(s, 0x0E, 0x00, *pkt)
    spi(s, 0x02, 0x43, 0xFF)
    if xosc:
        spi(s, 0x80, 0x01)
        time.sleep(0.02)
        if trim is not None:
            spi(s, 0x0D, 0x09, 0x11, trim, trim)
    t0 = time.time()
    spi(s, 0x83, 0x00, 0x00, 0x00)
    while time.time() - t0 < 10:
        v = irq(s)
        if v & 0x201:
            break
        time.sleep(0.02)
    r = spi(s, 0x17, 0, 0, 0)
    err = (r[2] << 8) | r[3]
    spi(s, 0x07, 0, 0)
    return time.time() - t0, v, err


for i, (name, length, regulator, trim, xosc, ramp, cr, heat, mhz, pre) in enumerate(CONFIGS):
    tx_setup(regulator, ramp)
    for k in range(N):
        dt, v, err = send(i, k, length, trim, xosc, cr, heat, mhz, pre)
        print("cfg %d %-32s pkt %d id=0x%08x  len=%3d  tx %.2fs irq=0x%03x err=0x%03x"
              % (i, name, k, 0xA0000000 | (i << 8) | k, length, dt, v, err), flush=True)
        time.sleep(GAP)
spi(s, 0x80, 0x00)
print("done")
