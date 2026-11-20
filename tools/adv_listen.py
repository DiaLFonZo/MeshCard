"""Listen to the Cardputer ADV log without restarting it (reset lines left alone).
Usage: python tools/adv_listen.py COM8 <seconds>
"""
import sys, time, serial
end = time.time() + float(sys.argv[2])
buf = b""
while time.time() < end:
    try:
        s = serial.Serial()
        s.port = sys.argv[1]
        s.baudrate = 115200
        s.timeout = 0.2
        s.dtr = False
        s.rts = False
        s.open()
        while time.time() < end:
            buf += s.read(4096)
    except Exception as e:
        buf += ("\n[port: %s]\n" % e).encode()
        time.sleep(0.5)
print(buf.decode("utf-8", "replace")[-5000:])
