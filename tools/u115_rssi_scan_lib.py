"""Shared helpers for the U115 bridge test tools (u115_cad_test.py): frames, SPI calls, LongFast radio set-up."""
import os
import struct
import time


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


def setup(s):
    """Reset the SX1262 and set it up like Meshtastic LongFast on 902.375 MHz (no transmit)."""
    time.sleep(0.2)
    s.reset_input_buffer()
    call(s, 0x03)
    time.sleep(0.05)
    spi(s, 0x80, 0x00)
    tcxo = os.environ.get("U115_TCXO")       # voltage code (2 = 1.8 V, as the maker's ASR6501 code), or unset
    if tcxo is not None:
        spi(s, 0x07, 0, 0)                   # clear the power-on errors
        delay = 320                          # 5 ms in 15.625 us steps (maker: 2 ms)
        spi(s, 0x97, int(tcxo), 0, (delay >> 8) & 0xFF, delay & 0xFF)
    spi(s, 0x96, 0x01)                       # DC-DC
    spi(s, 0x9D, 0x01)                       # DIO2 = RF switch
    spi(s, 0x8A, 0x01)                       # LoRa
    spi(s, 0x89, 0x7F)
    time.sleep(0.05)
    spi(s, 0x98, 0xE1, 0xE9)
    time.sleep(0.05)
    spi(s, 0x8B, 0x0B, 0x05, 0x01, 0x00)     # SF11 BW250 CR4/5
    spi(s, 0x8C, 0x00, 0x10, 0x00, 0xFF, 0x01, 0x00)  # preamble 16, explicit, 255, CRC on
    spi(s, 0x0D, 0x07, 0x40, 0x24, 0xB4)     # sync word 0x2B (Meshtastic)
    spi(s, 0x0D, 0x08, 0xAC, 0x96)           # boosted RX gain
    if tcxo is not None:
        r = spi(s, 0x17, 0, 0, 0)
        print("TCXO code %s: device errors after calibration 0x%03x" % (tcxo, (r[2] << 8) | r[3]), flush=True)
        spi(s, 0x07, 0, 0)
