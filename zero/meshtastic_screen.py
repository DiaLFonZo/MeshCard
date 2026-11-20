#!/usr/bin/env python3
"""Meshtastic's own screen (the BaseUI seen on Heltec / Cardputer ADV) on the Cardputer Zero.

meshtasticd runs as a service with a virtual 320x170 display. This app asks it to
mirror that display over the local TCP API (FromRadio.display_frame), paints each
frame on the Zero's LCD (/dev/fb0, RGB565), and sends the Zero's keyboard to it as
AdminMessage.send_input_event. Quitting (the launcher's long ESC, or SIGTERM)
turns mirroring off; the radio keeps running in the background.

Protobuf is encoded by hand (field numbers from src/mesh/generated/meshtastic/*.pb.h)
because the Python meshtastic package on the Zero predates display_frame.
"""
import mmap
import os
import select
import signal
import socket
import struct
import sys
import time

HOST, PORT = "127.0.0.1", 4403
FB, KBD = "/dev/fb0", "/dev/input/by-path/platform-3f804000.i2c-event"
ON, OFF = 0xFFFF, 0x0000  # white on black until Meshtastic sends its colour palette
DEBUG = bool(os.environ.get("MS_DEBUG"))
BIT_TABLES = [bytes(0xFF if v >> b & 1 else 0 for v in range(256)) for b in range(8)]

# input_broker_event codes (src/input/InputBroker.h)
SELECT, UP, DOWN, LEFT, RIGHT, CANCEL, ANYKEY = 10, 17, 18, 19, 20, 24, 0xFF


# ---------- minimal protobuf ----------
def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def f_varint(tag, n):
    return varint(tag << 3) + varint(n)


def f_bytes(tag, b):
    return varint(tag << 3 | 2) + varint(len(b)) + b


def f_fixed32(tag, n):
    return varint(tag << 3 | 5) + struct.pack("<I", n)


def parse(buf):
    """Yield (tag, wiretype, value) for one message level."""
    i, n = 0, len(buf)
    while i < n:
        key = 0
        shift = 0
        while True:
            b = buf[i]
            i += 1
            key |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                break
        tag, wt = key >> 3, key & 7
        if wt == 0:
            v = 0
            shift = 0
            while True:
                b = buf[i]
                i += 1
                v |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
        elif wt == 2:
            ln = 0
            shift = 0
            while True:
                b = buf[i]
                i += 1
                ln |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
            v = buf[i:i + ln]
            i += ln
        elif wt == 5:
            v = struct.unpack_from("<I", buf, i)[0]
            i += 4
        elif wt == 1:
            v = struct.unpack_from("<Q", buf, i)[0]
            i += 8
        else:
            return
        yield tag, wt, v


# ---------- API connection ----------
class Api:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        self.sock.setblocking(False)
        self.rx = bytearray()
        self.my_node = 0
        self.configured = False
        self.mirroring = False

    def send_toradio(self, payload):
        self.sock.setblocking(True)
        self.sock.sendall(b"\x94\xc3" + struct.pack(">H", len(payload)) + payload)
        self.sock.setblocking(False)

    def send_admin(self, admin):
        data = f_varint(1, 6) + f_bytes(2, admin)  # Data: portnum ADMIN_APP, payload
        pkt = f_fixed32(2, self.my_node) + f_bytes(4, data) + f_fixed32(6, int.from_bytes(os.urandom(4), "little") | 1)
        self.send_toradio(f_bytes(1, pkt))

    def start(self):
        self.send_toradio(f_varint(3, int.from_bytes(os.urandom(4), "little") | 1))  # want_config_id

    def set_mirror(self, on):
        self.send_admin(f_varint(51, 1 if on else 0))
        self.mirroring = on

    def key(self, code, ch=0):
        self.send_admin(f_bytes(27, f_varint(1, code) + (f_varint(2, ch) if ch else b"")))

    def heartbeat(self):
        self.send_toradio(f_bytes(7, b""))

    def read_messages(self):
        try:
            chunk = self.sock.recv(65536)
        except BlockingIOError:
            return []
        if not chunk:
            raise ConnectionError("meshtasticd closed the connection")
        self.rx += chunk
        msgs = []
        while True:
            start = self.rx.find(b"\x94\xc3")
            if start < 0:
                self.rx.clear()
                break
            if start:
                del self.rx[:start]
            if len(self.rx) < 4:
                break
            ln = struct.unpack(">H", self.rx[2:4])[0]
            if len(self.rx) < 4 + ln:
                break
            msgs.append(bytes(self.rx[4:4 + ln]))
            del self.rx[:4 + ln]
        return msgs


# ---------- screen ----------
class Screen:
    def __init__(self):
        self.w = int(open("/sys/class/graphics/fb0/virtual_size").read().split(",")[0])
        self.h = int(open("/sys/class/graphics/fb0/virtual_size").read().split(",")[1])
        self.stride = int(open("/sys/class/graphics/fb0/stride").read())
        self.fd = os.open(FB, os.O_RDWR)
        self.mm = mmap.mmap(self.fd, self.stride * self.h)
        self.on = struct.pack("<H", ON)
        self.off = struct.pack("<H", OFF)

    def clear(self):
        self.mm[:] = self.off * (len(self.mm) // 2)

    def message(self, text):
        # Tiny status text without fonts: just clear; the frame replaces it quickly.
        self.clear()

    def draw_mono(self, fw, fh, data, palette=None):
        """MONO_VLSB: byte = x + (y // 8) * width, bit = y % 8.
        palette = (default_on, default_off, [(x, y, w, h, on, off), ...]) in RGB565, or None for white on black."""
        w, h = min(fw, self.w), min(fh, self.h)
        ox, oy = (self.w - w) // 2, (self.h - h) // 2
        don, doff, regions = palette if palette else (ON, OFF, [])
        out = bytearray(self.stride * self.h)
        row = bytearray(2 * w)
        for y in range(h):
            page = (y >> 3) * fw
            # one byte per pixel: 0xFF if lit, 0x00 if not
            px = data[page:page + w].translate(BIT_TABLES[y & 7])
            lo_t, hi_t = color_tables(don, doff)
            row[0::2] = px.translate(lo_t)
            row[1::2] = px.translate(hi_t)
            for rx, ry, rw, rh, ron, roff in regions:  # later regions win, like the real panel
                if ry <= y < ry + rh:
                    xs, xe = max(rx, 0), min(rx + rw, w)
                    if xs < xe:
                        lo_t, hi_t = color_tables(ron, roff)
                        seg = px[xs:xe]
                        row[2 * xs:2 * xe:2] = seg.translate(lo_t)
                        row[2 * xs + 1:2 * xe:2] = seg.translate(hi_t)
            pos = (oy + y) * self.stride + ox * 2
            out[pos:pos + 2 * w] = row
        self.mm[:] = out


_COLOR_TABLES = {}


def color_tables(on, off):
    """Translate tables turning a lit/unlit byte (0xFF/0x00) into the low and high byte of an RGB565 colour."""
    key = (on, off)
    t = _COLOR_TABLES.get(key)
    if t is None:
        lo, hi = bytearray([off & 0xFF] * 256), bytearray([off >> 8] * 256)
        lo[0xFF], hi[0xFF] = on & 0xFF, on >> 8
        t = _COLOR_TABLES[key] = (bytes(lo), bytes(hi))
    return t


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


class Palette:
    """FromRadio.display_palette (tag 22): region colours, possibly split over several messages."""

    def __init__(self):
        self.sig, self.don, self.doff, self.total, self.regions = 0, ON, OFF, 0, {}

    def add(self, msg):
        f = {}
        regs = []
        for t, _, v in parse(msg):
            if t == 6:
                r = {t2: v2 for t2, _, v2 in parse(v)}
                regs.append((s16(r.get(1, 0)), s16(r.get(2, 0)), r.get(3, 0), r.get(4, 0), r.get(5, 0), r.get(6, 0)))
            else:
                f[t] = v
        sig, offset = f.get(1, 0), f.get(4, 0)
        if sig != self.sig:
            self.sig, self.regions = sig, {}
        if offset == 0:
            self.don, self.doff, self.total = f.get(2, 0), f.get(3, 0), f.get(5, 0)
        for i, r in enumerate(regs):
            self.regions[offset + i] = r

    def get(self, sig):
        """The palette for a frame with this signature, or None (render white on black)."""
        if not sig or sig != self.sig or len(self.regions) < self.total:
            return None
        return self.don, self.doff, [self.regions[i] for i in sorted(self.regions)]


# ---------- keyboard ----------
EV_KEY = 1
KEYMAP = {}
for i, c in enumerate("1234567890"):
    KEYMAP[2 + i] = c
for row, start in (("qwertyuiop", 16), ("asdfghjkl", 30), ("zxcvbnm", 44)):
    for i, c in enumerate(row):
        KEYMAP[start + i] = c
KEYMAP.update({57: " ", 12: "-", 13: "=", 26: "[", 27: "]", 39: ";", 40: "'", 41: "`", 43: "\\",
               51: ",", 52: ".", 53: "/", 14: "\b", 15: "\t"})
SHIFTED = {"1": "!", "2": "@", "3": "#", "4": "$", "5": "%", "6": "^", "7": "&", "8": "*", "9": "(",
           "0": ")", "-": "_", "=": "+", "[": "{", "]": "}", ";": ":", "'": '"', "`": "~", "\\": "|",
           ",": "<", ".": ">", "/": "?"}
SPECIAL = {103: UP, 108: DOWN, 105: LEFT, 106: RIGHT, 28: SELECT, 1: CANCEL}
SHIFT_KEYS = (42, 54)
EVENT = struct.Struct("qqHHi")


def other_client_connected():
    """True if some client holds an open connection to meshtasticd's API port (4403)."""
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for line in open(path).readlines()[1:]:
                f = line.split()
                if f[1].endswith(":%04X" % PORT) and f[3] == "01":  # local port 4403, ESTABLISHED
                    return True
        except OSError:
            pass
    return False


def main():
    screen = Screen()
    screen.clear()
    kbd = os.open(KBD, os.O_RDONLY | os.O_NONBLOCK)
    api = None
    shift = False
    frame_id, frame_buf = None, bytearray()
    palette, last = Palette(), None
    last_beat = time.time()
    running = [True]
    test_keys = [k for k in os.environ.get("MS_TEST_KEYS", "").split(",") if k]

    def stop(*_):
        running[0] = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGHUP, stop)

    while running[0]:
        if api is None:
            # our meshtasticd build keeps two connections: this screen (mirror-only) and the phone app
            try:
                api = Api()
                api.start()
            except OSError:
                api = None
                time.sleep(1)
                continue
        try:
            r, _, _ = select.select([api.sock, kbd], [], [], 0.5)
            if api.sock in r:
                for msg in api.read_messages():
                    if DEBUG:
                        print("rx", [t for t, _, _ in parse(msg)], len(msg), file=sys.stderr)
                    for tag, _, v in parse(msg):
                        if tag == 3:  # my_info
                            for t2, _, v2 in parse(v):
                                if t2 == 1:
                                    api.my_node = v2
                            if api.my_node and not api.mirroring:
                                # mirror right away: it marks this connection screen-only before any
                                # mesh traffic flows, so messages are left for the phone app
                                api.set_mirror(True)
                        elif tag == 7 and not api.configured:  # config_complete_id
                            api.configured = True
                            if not api.mirroring:
                                api.set_mirror(True)
                        elif tag == 21:  # display_frame
                            fr = {t: val for t, _, val in parse(v)}
                            if DEBUG:
                                print("frame", {k: (len(x) if isinstance(x, (bytes, bytearray)) else x) for k, x in fr.items()}, file=sys.stderr)
                            fid, off_, total = fr.get(4, 0), fr.get(5, 0), fr.get(6, 0)
                            if fid != frame_id or off_ != len(frame_buf):
                                frame_id, frame_buf = fid, bytearray()
                                if off_ != 0:
                                    continue
                            frame_buf += fr.get(7, b"")
                            if total and len(frame_buf) == total and fr.get(3, 0) == 1:
                                last = (fr.get(1, 0), fr.get(2, 0), bytes(frame_buf), fr.get(12, 0))
                                screen.draw_mono(last[0], last[1], last[2], palette.get(last[3]))
                        elif tag == 22:  # display_palette
                            palette.add(v)
                            if last and palette.get(last[3]):  # colours came after the frame: repaint it
                                screen.draw_mono(last[0], last[1], last[2], palette.get(last[3]))
            if kbd in r:
                data = os.read(kbd, EVENT.size * 32)
                for off in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    _, _, etype, code, value = EVENT.unpack_from(data, off)
                    if etype != EV_KEY:
                        continue
                    if code in SHIFT_KEYS:
                        shift = value != 0
                        continue
                    if value == 0 or not api.configured:  # act on press and auto-repeat
                        continue
                    if code in SPECIAL:
                        api.key(SPECIAL[code])
                    elif code in KEYMAP:
                        ch = KEYMAP[code]
                        if shift:
                            ch = ch.upper() if ch.isalpha() else SHIFTED.get(ch, ch)
                        api.key(ANYKEY, ord(ch))
            if api.configured and test_keys and time.time() - last_beat > 2:
                # MS_TEST_KEYS=20,20,10 or c,c,z : key codes or typed letters, 2 s apart (testing without hands)
                k = test_keys.pop(0)
                api.key(int(k)) if k.isdigit() else api.key(ANYKEY, ord(k))
                last_beat = time.time()
            if api.configured and time.time() - last_beat > 30:
                api.heartbeat()
                last_beat = time.time()
        except (OSError, ConnectionError) as e:
            if DEBUG:
                print("reconnect:", repr(e), file=sys.stderr)
            try:
                api.sock.close()
            except Exception:
                pass
            api = None
            screen.clear()
            time.sleep(1)

    if api is not None:
        try:
            api.set_mirror(False)
            api.send_toradio(f_varint(4, 1))  # disconnect
            api.sock.close()
        except Exception:
            pass
    screen.clear()


if __name__ == "__main__":
    sys.exit(main())
