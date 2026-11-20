"""Listen on Meshtastic US LongFast (906.875 MHz, SF11 BW250 CR4/5, sync 0x2B) straight through the bridge
(meshtasticd must be stopped). Prints the noise floor with the DIO2 RF switch on and off, then listens N seconds
and prints every preamble / header / packet the radio sees."""
import os
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 120


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


def rssi(s):
    r = x(s, [0x15, 0, 0])
    return -r[2] / 2 if r else None


def irq(s):
    r = x(s, [0x12, 0, 0, 0])
    return (r[2] << 8 | r[3]) if r else None


def setup(s, dio2):
    call(s, 0x03)
    time.sleep(0.05)
    x(s, [0x80, 0x00])
    x(s, [0x96, 0x00])                          # LDO
    x(s, [0x9D, 0x01 if dio2 else 0x00])        # DIO2 as RF switch
    x(s, [0x89, 0x7F])
    time.sleep(0.05)
    x(s, [0x80, 0x01])
    x(s, [0x98, 0xE1, 0xE9])
    time.sleep(0.05)
    x(s, [0x8A, 0x01])                          # LoRa
    fr = int(round(float(os.environ.get("RX_MHZ", "906.875")) * 1e6 * (1 << 25) / 32e6))
    x(s, [0x86, fr >> 24 & 0xFF, fr >> 16 & 0xFF, fr >> 8 & 0xFF, fr & 0xFF])  # default 906.875 MHz
    x(s, [0x8B, 0x0B, 0x05, 0x01, 0x00])        # SF11 BW250 CR4/5
    x(s, [0x8C, 0x00, 0x10, 0x00, 0xFF, 0x01, 0x00])  # preamble 16, explicit, CRC on
    x(s, [0x0D, 0x07, 0x40, 0x24, 0xB4])        # sync word 0x2B
    x(s, [0x0D, 0x08, 0xAC, 0x96])              # RX boosted gain
    x(s, [0x8F, 0x00, 0x00])
    x(s, [0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0])
    x(s, [0x02, 0xFF, 0xFF])
    x(s, [0x82, 0xFF, 0xFF, 0xFF])              # RX continuous
    time.sleep(0.05)
    st = x(s, [0xC0, 0])
    sw = x(s, [0x1D, 0x07, 0x40, 0, 0, 0])
    print(f"  status=0x{st[1]:02X} sync=0x{sw[4]:02X}{sw[5]:02X}" if st and sw else "  no reply")


def floor(s, label):
    v = [rssi(s) for _ in range(40)]
    v = [a for a in v if a is not None]
    print(f"  {label}: noise floor avg {sum(v)/len(v):.1f} dBm, min {min(v)}, max {max(v)}")


with serial.Serial(PORT, 115200, timeout=0.2) as s:
    time.sleep(0.2)
    s.reset_input_buffer()
    c, body = call(s, 0x01)
    print("bridge:", body[:6].decode(errors="replace") if c == 0x81 else "no reply")
    print("bridge version:", body[6] if c == 0x81 and len(body) > 6 else "?")
    setup(s, True)
    for on in (0, 1, 0, 1):
        c5, st5 = call(s, 0x05, bytes([on]))
        time.sleep(0.05)
        floor(s, f"antenna switch power {'ON ' if on else 'OFF'} (reply {st5.hex() if c5 == 0x85 else 'none'})")
    # Inverted-switch test: DIO2 as plain IRQ output, held high by an uncleared CadDone.
    for high in (False, True, False, True):
        setup(s, False)
        x(s, [0x80, 0x01])
        x(s, [0x08, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80 if high else 0x00, 0, 0])  # dio2 mask = CadDone
        x(s, [0x02, 0xFF, 0xFF])
        x(s, [0x88, 0x04, 0x16, 0x0A, 0x00, 0x00, 0x00, 0x00])  # CAD params, CAD_ONLY
        x(s, [0xC5])                                              # SetCad -> CadDone sets DIO2 if mapped
        time.sleep(0.2)
        x(s, [0x82, 0xFF, 0xFF, 0xFF])
        time.sleep(0.05)
        c4, pins = call(s, 0x04)
        floor(s, f"DIO2 forced {'HIGH' if high else 'low '} irq=0x{irq(s):04X}")
    KEEP = 0x0080 if "--dio2high" in sys.argv else 0
    if not KEEP:
        setup(s, True)  # normal LongFast listening, DIO2 drives the switch
    print(f"listening {SECS}s ... (DIO2 {'HIGH' if KEEP else 'normal'})")
    t0, peak, seen = time.time(), -200, 0
    while time.time() - t0 < SECS:
        f = irq(s)
        f = f & ~KEEP if f is not None else None
        r = rssi(s)
        if r is not None and r > peak:
            peak = r
        if f:
            names = [n for b, n in [(1, "RXDONE"), (2, "PREAMBLE"), (3, "SYNC"), (4, "HEADER"), (5, "HDR_ERR"), (6, "CRC_ERR"), (9, "TIMEOUT")] if f >> b & 1]
            line = f"  {time.time()-t0:6.1f}s irq=0x{f:04X} {names} rssi_now={r}"
            fe = x(s, [0x1D, 0x07, 0x6B, 0, 0, 0, 0])  # frequency error indicator (RadioLib getFrequencyError)
            if fe:
                raw = (fe[4] << 16 | fe[5] << 8 | fe[6]) & 0x0FFFFF
                if raw & 0x80000:
                    raw -= 1 << 20
                line += f" freq_err={1.55 * raw / (1600.0 / 250):+.0f} Hz"
            if f & 0x02:
                b = x(s, [0x13, 0, 0, 0])
                p = x(s, [0x14, 0, 0, 0, 0])
                if b and p:
                    line += f" len={b[2]} pkt_rssi={-p[2]/2} snr={struct.unpack('b', bytes([p[3]]))[0]/4}"
                    d = x(s, [0x1E, b[3], 0] + [0] * b[2])
                    if d and os.environ.get("RX_DUMP"):
                        with open(os.environ["RX_DUMP"], "a") as fh:
                            fh.write(bytes(d[3:]).hex() + "\n")
                    if d and len(d) >= 19:
                        h = d[3:]
                        line += (f" to={struct.unpack('<I', h[0:4])[0]:08x} from={struct.unpack('<I', h[4:8])[0]:08x}"
                                 f" chan_hash={h[13]}")
            print(line, flush=True)
            seen += 1
            m = 0xFFFF & ~KEEP
            x(s, [0x02, m >> 8, m & 0xFF])
        time.sleep(0.02)
    st = x(s, [0x10, 0, 0, 0, 0, 0, 0, 0])
    if st:
        print(f"stats: received={st[2]<<8|st[3]} crc_err={st[4]<<8|st[5]} hdr_err={st[6]<<8|st[7]}")
    print(f"events={seen}, peak instant rssi={peak}")
