"""Check the U115 firmware is running: sample the program counter a few times,
reset the chip, and listen on the serial port for anything it sends."""
import sys
import time

import serial
from pyocd.core.helpers import ConnectHelper

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"

session = None
for attempt in range(8):
    try:
        session = ConnectHelper.session_with_chosen_probe(
            target_override="cy8c4147azi-s475", connect_mode="attach", frequency=400000,
            options={"resume_on_disconnect": True, "reset_type": "hw"})
        session.open()
        break
    except Exception:
        print(f"connect attempt {attempt + 1} failed")
        session = None
        time.sleep(0.3)
if session is None:
    print("could not connect over SWD")
    sys.exit(1)

with session:
    t = session.target
    for i in range(5):
        for _ in range(5):
            try:
                t.halt()
                pc = t.read_core_register("pc")
                t.resume()
                print(f"pc sample {i}: 0x{pc:08X}")
                break
            except Exception:
                time.sleep(0.05)
        time.sleep(0.1)

    with serial.Serial(PORT, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        for _ in range(5):
            try:
                t.reset()
                break
            except Exception:
                time.sleep(0.05)
        data = b""
        end = time.time() + 2
        while time.time() < end:
            data += s.read(256)
        print(f"bytes on {PORT} after reset: {len(data)} -> {data[:64].hex()}")
