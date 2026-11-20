"""Run right after psoc4_acquire.py (chip halted in boot ROM, SWD alive).

Starts the app by hand (SP/PC from the app vector table) and watches it over
raw SWD: optional breakpoint, then halts every 200 ms to sample PC/LR, so we
see where the firmware goes and when (if) the SWD link dies.
Usage: python watch_boot.py [breakpoint_hex] [app_base_hex]
"""
import struct
import sys
import time

import libusb_package
import usb.core
import usb.util

EP_OUT, EP_IN = 0x04, 0x85
bp = int(sys.argv[1], 16) if len(sys.argv) > 1 and sys.argv[1] != "-" else None
app = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x2200

dev = usb.core.find(idVendor=0x2E8A, idProduct=0x000C, backend=libusb_package.get_libusb1_backend())
usb.util.claim_interface(dev, 0)


def xfer(pkt):
    dev.write(EP_OUT, bytes(pkt), 500)
    return bytes(dev.read(EP_IN, 512, 500))


def w(req, val):
    return bytes([req]) + struct.pack("<I", val)


def transfer(*reqs):
    return bytes([0x05, 0x00, len(reqs)]) + b"".join(reqs)


def recover():
    xfer(bytes([0x12, 72]) + b"\xff" * 7 + b"\x00\x00")
    xfer(transfer(bytes([0x02]), w(0x00, 0x1E), w(0x04, 0x54000000), w(0x08, 0), w(0x01, 0x23000002)))


def rd(a):
    r = xfer(transfer(w(0x05, a), bytes([0x0F])))
    if r[1] != 2 or r[2] != 1:
        raise IOError("rd")
    return struct.unpack("<I", r[3:7])[0]


def wr(a, v):
    r = xfer(transfer(w(0x05, a), w(0x0D, v)))
    if r[1] != 2 or r[2] != 1:
        raise IOError("wr")


def retry(fn, *a, n=10):
    for i in range(n):
        try:
            return fn(*a)
        except Exception:
            try:
                recover()
            except Exception:
                pass
    raise IOError("link lost")


DHCSR, DCRSR, DCRDR = 0xE000EDF0, 0xE000EDF4, 0xE000EDF8


def rreg(n):
    retry(wr, DCRSR, n)
    return retry(rd, DCRDR)


def wreg(n, v):
    retry(wr, DCRDR, v)
    retry(wr, DCRSR, n | 0x10000)


recover()
retry(wr, DHCSR, 0xA05F0003)                 # make sure halted
sp, pc = retry(rd, app), retry(rd, app + 4)
wreg(13, sp); wreg(15, pc & ~1); wreg(16, 0x01000000)   # SP, PC, xPSR (thumb)
retry(wr, 0x40030014, 0)                     # clear TEST_MODE
if bp is not None:
    retry(wr, 0xE0002000, 3)                 # FP_CTRL enable
    half = 0x80000000 if bp & 2 else 0x40000000
    retry(wr, 0xE0002008, half | (bp & 0x1FFFFFFC) | 1)
print(f"start SP=0x{sp:08X} PC=0x{pc:08X} bp={hex(bp) if bp is not None else '-'}")
retry(wr, DHCSR, 0xA05F0001)                 # run
t0 = time.time()
for i in range(15):
    time.sleep(0.2)
    try:
        d = retry(rd, DHCSR, n=5)
        if not d & (1 << 17):
            retry(wr, DHCSR, 0xA05F0003)
        p, lr = rreg(15), rreg(14)
        print(f"{time.time()-t0:4.1f}s halted={'bp' if d & (1<<17) else 'sample'} lockup={bool(d & (1<<19))} PC=0x{p:08X} LR=0x{lr:08X}"
              f" HSIOM3=0x{retry(rd, 0x40020300):08X}")
        if bp is not None and d & (1 << 17):
            break
        retry(wr, DHCSR, 0xA05F0001)
    except Exception as e:
        print(f"{time.time()-t0:4.1f}s SWD LOST ({e})")
        break
usb.util.release_interface(dev, 0)
