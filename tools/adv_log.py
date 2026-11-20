"""Read the Cardputer ADV log over USB.
Usage: python tools/adv_log.py [COM8] [seconds=20] [lines kept=120]
Note: opening the port this way can restart the ADV. To listen without restarting it,
use tools/adv_listen.py instead.
"""
import sys, time, serial
port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20
end = time.time() + secs
buf = b""
while time.time() < end:
    try:
        with serial.Serial(port, 115200, timeout=0.2) as s:
            while time.time() < end:
                buf += s.read(4096)
    except Exception:
        time.sleep(0.5)
text = buf.decode("utf-8", "replace")
out = [l for l in text.splitlines() if l.strip()]
keep = int(sys.argv[3]) if len(sys.argv) > 3 else 120
print("\n".join(out[-keep:]))
