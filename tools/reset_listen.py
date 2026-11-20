"""Pulse the U115 RST pad through the Debugprobe (raw CMSIS-DAP, no SWD traffic),
then listen on the serial port for the bridge's startup frame (5A E0 ...)."""
import struct
import sys
import time

import libusb_package
import serial
import usb.core
import usb.util

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
NRESET = 0x80

dev = usb.core.find(idVendor=0x2E8A, idProduct=0x000C, backend=libusb_package.get_libusb1_backend())
if dev is None:
    sys.exit("Debugprobe not found")
usb.util.claim_interface(dev, 0)


def xfer(pkt):
    dev.write(0x04, bytes(pkt), 500)
    return bytes(dev.read(0x85, 512, 500))


try:
    xfer([0x02, 0x01])  # DAP_Connect SWD (the probe only drives its pins once connected)
    with serial.Serial(PORT, BAUD, timeout=0.1) as s:
        s.reset_input_buffer()
        xfer([0x10, 0x00, NRESET] + list(struct.pack("<I", 0)))       # reset low
        time.sleep(0.05)
        xfer([0x10, NRESET, NRESET] + list(struct.pack("<I", 0)))     # reset high
        data = b""
        end = time.time() + 2.5
        while time.time() < end:
            data += s.read(256)
        print(f"bytes after reset: {len(data)} -> {data[:48].hex(' ')}")
        if b"\x5a\xe0" in data:
            print("BRIDGE STARTED OK")
finally:
    try:
        xfer([0x03])  # DAP_Disconnect
    except Exception:
        pass
    usb.util.release_interface(dev, 0)
