"""Print the Zero keyboard's device-tree keymaps (row, col, evdev code) and modifier codes."""
import struct

D = "/proc/device-tree/soc/i2c@7e804000/tca8418@34/"
for p in ("fn-button-code", "shift-button-code", "sym-button-code"):
    b = open(D + p, "rb").read()
    print(p, struct.unpack(">%dI" % (len(b) // 4), b))
for p in ("linux,keymap", "linux,keymap1", "linux,keymap2"):
    b = open(D + p, "rb").read()
    v = struct.unpack(">%dI" % (len(b) // 4), b)
    print(p, [((x >> 24) & 255, (x >> 16) & 255, x & 0xFFFF) for x in v])
