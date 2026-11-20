"""Does the U115's SX1262 run from a crystal or a DIO3-powered TCXO? Talks to the bridge directly
(meshtasticd must be stopped). Repeats the CubeCell start-up: standby RC, DIO3 TCXO 1.8 V, calibrate,
then standby XOSC, and prints status + device errors after each step, for both clock choices."""
import struct
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial0"
ERRS = ["RC64K_CALIB", "RC13M_CALIB", "PLL_CALIB", "ADC_CALIB", "IMG_CALIB", "XOSC_START", "PLL_LOCK", "?", "PA_RAMP"]
MODES = {2: "STBY_RC", 3: "STBY_XOSC", 4: "FS", 5: "RX", 6: "TX"}
CMDST = {0: "-", 1: "rfu", 2: "data avail", 3: "CMD TIMEOUT", 4: "CMD INVALID", 5: "CMD FAILED", 6: "tx done"}


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


def xfer(s, data):
    c, body = call(s, 0x02, bytes(data))
    if c != 0x82:
        return None
    return body


def status(s, label):
    r = xfer(s, [0xC0, 0x00])
    st = r[1] if r else None
    e = xfer(s, [0x17, 0, 0, 0])
    err = (e[2] << 8 | e[3]) if e else None
    names = [n for i, n in enumerate(ERRS) if err is not None and err >> i & 1]
    print(f"  {label:34s} status=0x{st:02X} mode={MODES.get(st >> 4 & 7, '?'):9s} cmd={CMDST.get(st >> 1 & 7)}"
          f"  errors=0x{err:04X} {names}" if st is not None and err is not None else f"  {label}: no reply")


def run(s, tcxo):
    print("TCXO 1.8 V on DIO3" if tcxo else "Crystal (no TCXO)")
    call(s, 0x03)  # reset radio
    time.sleep(0.05)
    xfer(s, [0x80, 0x00])  # standby RC
    xfer(s, [0x07, 0x00, 0x00])  # clear device errors
    status(s, "after reset + standby RC")
    if tcxo:
        xfer(s, [0x97, 0x02, 0x00, 0x00, 0x80])  # SetDio3AsTcxoCtrl 1.8 V, 2 ms (CubeCell values)
        status(s, "after SetDio3AsTcxoCtrl")
    xfer(s, [0x89, 0x7F])  # calibrate all
    time.sleep(0.05)
    status(s, "after Calibrate(0x7F)")
    xfer(s, [0x07, 0x00, 0x00])
    xfer(s, [0x80, 0x01])  # standby XOSC
    time.sleep(0.01)
    status(s, "after standby XOSC")
    xfer(s, [0x98, 0xE1, 0xE9])  # CalibrateImage 902-928 MHz
    time.sleep(0.05)
    status(s, "after CalibrateImage 902-928")
    xfer(s, [0x86, 0x38, 0xAE, 0x00, 0x00])  # SetRfFrequency ~906.875 MHz (0x38AE0000 * 32e6 / 2^25)
    xfer(s, [0x8A, 0x01])  # packet type LoRa
    xfer(s, [0xC1])  # SetFs
    time.sleep(0.01)
    status(s, "after SetFs (PLL lock test)")
    print()


with serial.Serial(PORT, 115200, timeout=0.2) as s:
    time.sleep(0.2)
    s.reset_input_buffer()
    c, body = call(s, 0x01)
    print("bridge:", body[:6].decode(errors="replace") if c == 0x81 else "no reply")
    run(s, tcxo=False)
    run(s, tcxo=True)
