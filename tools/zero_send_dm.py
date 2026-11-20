#!/usr/bin/env python3
# Run ON the Cardputer Zero: send one direct text message through the local radio service (port 4403).
# Handy to test the ADV receiving messages.
# Usage: python3 zero_send_dm.py <dest node, hex like 0x3c096782> "text"
import os, socket, struct, sys, time

dest = int(sys.argv[1], 16)
text = (sys.argv[2] if len(sys.argv) > 2 else "test").encode()


def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            return out + bytes([b])


def field_bytes(num, data):
    return varint(num << 3 | 2) + varint(len(data)) + data


data = varint(1 << 3 | 0) + varint(1) + field_bytes(2, text)          # Data{portnum=TEXT, payload}
pid = struct.unpack("<I", os.urandom(4))[0] & 0x7FFFFFFF or 1
pkt = (varint(2 << 3 | 5) + struct.pack("<I", dest)                  # to
       + field_bytes(4, data)                                         # decoded
       + varint(6 << 3 | 5) + struct.pack("<I", pid)                  # id
       + varint(9 << 3 | 0) + varint(3))                              # hop_limit
msg = field_bytes(1, pkt)                                             # ToRadio{packet}
s = socket.create_connection(("127.0.0.1", 4403), timeout=5)
s.sendall(b"\x94\xc3" + struct.pack(">H", len(msg)) + msg)
time.sleep(2)
s.close()
print("sent", text.decode(), hex(pid))
