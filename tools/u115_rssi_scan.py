"""Measure the noise the U115 radio hears, frequency by frequency (receive only, never transmits).
Run on the Cardputer Zero with the radio service stopped:  python3 u115_rssi_scan.py [/dev/serial0] [start end step MHz]
Prints, for each frequency, the average and the peak of the radio's instant RSSI. A peak well above the rest
is a signal or noise source on that frequency."""
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
F0, F1, STEP = (float(x) for x in (sys.argv[2:5] if len(sys.argv) > 4 else ("900", "930", "0.25")))
READS = 30


def frame(cmd, payload=b""):
    hdr = bytes([cmd]) + struct.pack("<H", len(payload))
    chk = 0
    for b in hdr + payload:
        chk ^= b
    return b"\xA5" + hdr + payload + bytes([chk])


def read_frame(s, timeout=1.0):
    end = time.time() + timeout
    while time.time() < end:
        b = s.read(1)
        if b != b"\x5A":
            continue
        hdr = s.read(3)
        if len(hdr) < 3:
            return None
        cmd, ln = hdr[0], struct.unpack("<H", hdr[1:])[0]
        body = s.read(ln + 1)
        if len(body) < ln + 1:
            return None
        return cmd, body[:-1]
    return None


def call(s, cmd, payload=b""):
    s.write(frame(cmd, payload))
    end = time.time() + 1.0
    while time.time() < end:
        r = read_frame(s, end - time.time())
        if r is None:
            return None
        if r[0] in (0xE0, 0xE1):
            continue
        return r
    return None


def spi(s, *b):
    r = call(s, 0x02, bytes(b))
    if r is None or r[0] != 0x82:
        raise RuntimeError("SPI failed: %r" % (r,))
    return r[1]


def set_freq(s, mhz):
    v = int(round(mhz * 1e6 * (1 << 25) / 32e6))
    spi(s, 0x80, 0x00)                      # standby RC
    spi(s, 0x86, *struct.pack(">I", v))     # SetRfFrequency
    spi(s, 0x82, 0xFF, 0xFF, 0xFF)          # SetRx continuous
    time.sleep(0.01)


def rssi(s):
    r = spi(s, 0x15, 0x00, 0x00)            # GetRssiInst
    return -r[2] / 2.0


s = serial.Serial(PORT, 115200, timeout=0.2)
time.sleep(0.2)
s.reset_input_buffer()
call(s, 0x03)                               # hard reset the SX1262
time.sleep(0.05)
spi(s, 0x80, 0x00)                          # standby
spi(s, 0x96, 0x01)                          # DC-DC (as the hosts now use)
spi(s, 0x9D, 0x01)                          # DIO2 drives the RF switch
spi(s, 0x8A, 0x01)                          # LoRa
spi(s, 0x89, 0x7F)                          # calibrate all
time.sleep(0.05)
spi(s, 0x98, 0xE1, 0xE9)                    # image calibration 902-928
time.sleep(0.05)
spi(s, 0x8B, 0x0B, 0x05, 0x01, 0x00)        # SF11, BW250, CR4/5, LDRO off (LongFast)
spi(s, 0x0D, 0x08, 0xAC, 0x96)              # RX boosted gain, as Meshtastic
f = F0
rows = []
while f <= F1 + 1e-6:
    set_freq(s, f)
    vals = [rssi(s) for _ in range(READS)]
    rows.append((f, sum(vals) / len(vals), max(vals)))
    print("%8.3f MHz  avg %6.1f  peak %6.1f dBm" % rows[-1], flush=True)
    f += STEP
spi(s, 0x80, 0x00)
best = sorted(rows, key=lambda r: r[1])
print("quietest avg %.1f dBm at %.3f MHz, noisiest avg %.1f dBm at %.3f MHz"
      % (best[0][1], best[0][0], best[-1][1], best[-1][0]))
