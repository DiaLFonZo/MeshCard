"""Talk to the U115 bridge firmware over a serial port and prove the radio answers.

Usage: python bridge_test.py COM6 [baud]
Steps: ping the bridge, reset the radio, read the SX1262 status and its
LoRa sync-word register (0x0740, factory default 0x14 0x24).
"""
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200


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
        chk = 0
        for x in hdr + body[:-1]:
            chk ^= x
        if chk != body[-1]:
            print("  (bad checksum on reply)")
            return None
        return cmd, body[:-1]
    return None


def call(s, cmd, payload=b"", want=None):
    s.write(frame(cmd, payload))
    end = time.time() + 1.0
    while time.time() < end:
        r = read_frame(s, end - time.time())
        if r is None:
            return None
        if r[0] in (0xE0, 0xE1):  # events
            print(f"  event 0x{r[0]:02X} {r[1].hex()}")
            continue
        return r
    return None


with serial.Serial(PORT, BAUD, timeout=0.2) as s:
    time.sleep(0.2)
    s.reset_input_buffer()

    r = call(s, 0x01)
    print("PING   ->", r and (r[0] == 0x81 and r[1][:6].decode(errors="replace"), "v%d" % r[1][6]) or "no reply")
    if not r:
        sys.exit(1)

    r = call(s, 0x03)
    print("RESET  ->", "ok" if r and r[0] == 0x83 else r)
    time.sleep(0.05)

    r = call(s, 0x04)
    print("PINS   ->", r and f"BUSY={r[1][0]} DIO1={r[1][1]}")

    r = call(s, 0x02, bytes([0xC0, 0x00]))  # GetStatus
    if r and r[0] == 0x82:
        st = r[1][1]
        mode = {2: "STBY_RC", 3: "STBY_XOSC", 4: "FS", 5: "RX", 6: "TX"}.get((st >> 4) & 7, "?")
        print(f"STATUS -> 0x{st:02X} (mode {mode})")
    else:
        print("STATUS ->", r)

    r = call(s, 0x02, bytes([0x1D, 0x07, 0x40, 0x00, 0x00, 0x00]))  # ReadRegister 0x0740, 2 bytes
    if r and r[0] == 0x82:
        sw = r[1][4:6]
        ok = sw == b"\x14\x24"
        print(f"SYNCWORD -> {sw.hex()}  {'RADIO OK (factory default 1424)' if ok else 'unexpected'}")
    else:
        print("SYNCWORD ->", r)
