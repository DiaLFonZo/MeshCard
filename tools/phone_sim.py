"""Act like the phone app: connect to the Zero's Meshtastic over Wi-Fi, ask for config, stay connected N seconds.
Prints what it received, so we can check the phone and the Zero's screen app can be connected together.
Usage: python phone_sim.py [seconds]"""
import os
import socket
import struct
import sys
import time

HOST, PORT = "192.168.0.44", 4403
secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def send(sock, payload):
    sock.sendall(b"\x94\xc3" + struct.pack(">H", len(payload)) + payload)


s = socket.create_connection((HOST, PORT), timeout=5)
send(s, varint(3 << 3) + varint(int.from_bytes(os.urandom(4), "little") | 1))  # want_config_id
s.settimeout(1)
if len(sys.argv) > 2:  # phone_sim.py 20 "hello": also broadcast a text on the primary channel
    text = sys.argv[2].encode()
    data = varint(1 << 3) + varint(1) + varint(2 << 3 | 2) + varint(len(text)) + text  # portnum TEXT_MESSAGE_APP
    pkt = (varint(2 << 3 | 5) + struct.pack("<I", 0xFFFFFFFF) + varint(4 << 3 | 2) + varint(len(data)) + data
           + varint(6 << 3 | 5) + struct.pack("<I", int.from_bytes(os.urandom(4), "little") | 1)
           + varint(9 << 3) + varint(3)     # hop_limit 3, like the phone app: other nodes repeat it
           + varint(10 << 3) + varint(1))   # want_ack: the Zero listens for that repeat (implicit ACK)
    time.sleep(2)
    send(s, varint(1 << 3 | 2) + varint(len(pkt)) + pkt)
    print("phone_sim: sent text", sys.argv[2])
rx, counts, t0, last_beat = bytearray(), {}, time.time(), time.time()
alive = True
while time.time() - t0 < secs:
    try:
        chunk = s.recv(65536)
        if not chunk:
            alive = False
            print("phone_sim: Meshtastic closed our connection after %.1f s" % (time.time() - t0))
            break
        rx += chunk
    except socket.timeout:
        pass
    while len(rx) >= 4 and rx[:2] == b"\x94\xc3":
        ln = struct.unpack(">H", rx[2:4])[0]
        if len(rx) < 4 + ln:
            break
        msg = rx[4:4 + ln]
        del rx[:4 + ln]
        tag = msg[0] >> 3 if msg else 0
        counts[tag] = counts.get(tag, 0) + 1
    if rx and rx[:2] != b"\x94\xc3":
        i = rx.find(b"\x94\xc3")
        del rx[:i if i > 0 else len(rx)]
    if time.time() - last_beat > 10:
        send(s, varint(7 << 3 | 2) + b"\x00")  # heartbeat
        last_beat = time.time()
print("phone_sim: still connected" if alive else "phone_sim: dropped", "| FromRadio tags received:", dict(sorted(counts.items())))
s.close()
