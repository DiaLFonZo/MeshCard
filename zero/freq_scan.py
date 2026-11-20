"""Find where the other nodes' LongFast signal really is, as seen by the U115 (meshtasticd stopped).
Sweeps -400..+400 kHz around 906.875 MHz in 25 kHz steps with a narrow (62.5 kHz) filter, over and over,
keeping the strongest instant RSSI per offset. A real transmission shows up as a bump; its centre is the
frequency error between the U115 and the other node."""
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 120
F0 = 906.875e6
WIDE = len(sys.argv) > 3 and sys.argv[3] == "wide"
# wide: the whole US band 902-928 MHz in 250 kHz steps (offsets from F0); else +-400 kHz in 25 kHz steps
OFFS = list(range(-4875000, 21125001, 250000)) if WIDE else list(range(-400000, 400001, 25000))


def frame(cmd, payload=b""):
    hdr = bytes([cmd]) + struct.pack("<H", len(payload))
    chk = 0
    for b in hdr + payload:
        chk ^= b
    return b"\xA5" + hdr + payload + bytes([chk])


def call(s, cmd, payload=b""):
    s.write(frame(cmd, payload))
    end = time.time() + 1.0
    while time.time() < end:
        if s.read(1) != b"\x5A":
            continue
        hdr = s.read(3)
        c, ln = hdr[0], struct.unpack("<H", hdr[1:])[0]
        body = s.read(ln + 1)[:-1]
        if c in (0xE0, 0xE1):
            continue
        return c, body
    return None, b""


def x(s, data):
    c, body = call(s, 0x02, bytes(data))
    return body if c == 0x82 else None


def setfreq(s, hz):
    r = int(round(hz * (1 << 25) / 32e6))
    x(s, [0x86, r >> 24 & 0xFF, r >> 16 & 0xFF, r >> 8 & 0xFF, r & 0xFF])


with serial.Serial(PORT, 115200, timeout=0.2) as s:
    time.sleep(0.2)
    s.reset_input_buffer()
    c, body = call(s, 0x01)
    print("bridge:", body[:6].decode(errors="replace") if c == 0x81 else "no reply", flush=True)
    call(s, 0x03)
    time.sleep(0.05)
    x(s, [0x80, 0x00])
    x(s, [0x96, 0x00])
    x(s, [0x9D, 0x01])
    x(s, [0x89, 0x7F])
    time.sleep(0.05)
    x(s, [0x80, 0x01])
    x(s, [0x98, 0xE1, 0xE9])
    time.sleep(0.05)
    x(s, [0x8A, 0x01])
    x(s, [0x8B, 0x07, 0x06 if WIDE else 0x04, 0x01, 0x00])  # SF7, BW 500 (wide) / 125 kHz
    x(s, [0x0D, 0x08, 0xAC, 0x96])
    peak = {o: -200.0 for o in OFFS}
    base = {o: [] for o in OFFS}
    t0, sweeps = time.time(), 0
    while time.time() - t0 < SECS:
        for o in OFFS:
            x(s, [0x80, 0x01])
            setfreq(s, F0 + o)
            x(s, [0x82, 0xFF, 0xFF, 0xFF])
            time.sleep(0.006)  # let the RSSI reading settle
            r = x(s, [0x15, 0, 0])
            if r:
                v = -r[2] / 2
                base[o].append(v)
                peak[o] = max(peak[o], v)
        sweeps += 1
    print(f"{sweeps} sweeps in {SECS}s")
    for o in OFFS:
        b = sorted(base[o])[len(base[o]) // 2] if base[o] else 0
        bar = "#" * max(0, int((peak[o] - b) / 2))
        print(f"  {(F0 + o)/1e6:8.3f} MHz ({o/1000:+6.0f} kHz)  median {b:6.1f}  peak {peak[o]:6.1f}  {bar}")
