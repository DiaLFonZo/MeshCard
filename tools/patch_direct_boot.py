"""Make a CubeCell .hex boot straight into the app, skipping the Heltec UART
bootloader: copy the app's stack pointer + reset address (at 0x2200) into the
chip's start vector (0x0). Usage: python patch_direct_boot.py in.hex out.hex"""
import sys
from intelhex import IntelHex

APP = 0x2200
ih = IntelHex(sys.argv[1])
for i in range(8):
    ih[i] = ih[APP + i]
ih.write_hex_file(sys.argv[2])
print("patched start vector ->", hex(int.from_bytes(bytes(ih[APP + i] for i in range(4, 8)), "little")))
