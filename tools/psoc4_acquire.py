"""PSoC4 / ASR6501 "acquire" through a Raspberry Pi Debugprobe (CMSIS-DAP v2).

Use when the firmware on the chip switches off the SWD pins. Pulses XRES, then
in ONE USB packet (run back-to-back on the Pico): releases reset, waits a few
microseconds, does an SWD line reset, reads IDCODE and sets TEST_MODE, which
keeps the chip in its boot ROM instead of starting the user firmware. Then it
halts the CPU, turns off the watchdog and reports the port-3 pin function
register (SWD pins) as the chip sets it at power-up.

After it prints "ACQUIRED", run flash_robust.py right away (do not unplug).
"""
import struct
import sys
import time

import libusb_package
import usb.core

EP_OUT, EP_IN = 0x04, 0x85
NRESET = 0x80

dev = usb.core.find(idVendor=0x2E8A, idProduct=0x000C, backend=libusb_package.get_libusb1_backend())
if dev is None:
    sys.exit("Debugprobe not found")
usb.util.claim_interface(dev, 0)


LOG = open(__file__ + ".log", "w")


def xfer(pkt, name="?"):
    LOG.write(f"-> {name}: {bytes(pkt)[:24].hex()}\n"); LOG.flush()
    dev.write(EP_OUT, bytes(pkt), 500)
    r = bytes(dev.read(EP_IN, 512, 500))
    LOG.write(f"<- {r[:24].hex()}\n"); LOG.flush()
    return r


def w(req, val):
    return bytes([req]) + struct.pack("<I", val)


def transfer(*reqs):
    body = b"".join(reqs)
    n = sum(1 for _ in reqs)
    return bytes([0x05, 0x00, n]) + body


def swj_pins(out, sel, wait_us=0):
    return bytes([0x10, out, sel]) + struct.pack("<I", wait_us)


def delay(us):
    return bytes([0x09]) + struct.pack("<H", us)


LINE_RESET = bytes([0x12, 72]) + b"\xff" * 7 + b"\x00\x00"  # 56 ones + 16 zeros

# set up probe: SWD, 2 MHz, short waits
print("packet size:", xfer([0x00, 0xFF], "info pkt size").hex())
xfer([0x02, 0x01], "connect")                                     # DAP_Connect SWD
xfer([0x11] + list(struct.pack("<I", 2_000_000)), "clock")        # DAP_SWJ_Clock
xfer([0x04, 0x00] + list(struct.pack("<HH", 64, 0)), "tcfg")      # TransferConfigure
print("packet count:", xfer([0x00, 0xFE], "info pkt count").hex())
# NOTE: DAP_ExecuteCommands (0x7F) with reset/transfer inside freezes the
# Debugprobe firmware (needs a USB replug), so it is not used. Instead the
# packets are written back-to-back and the answers read afterwards.


def try_acquire(d_us):
    xfer(swj_pins(0x00, NRESET, 0), "hold reset")
    time.sleep(0.005)
    pkts = [swj_pins(NRESET, NRESET, 0)]  # release reset
    if d_us:
        pkts.append(delay(d_us))
    pkts += [
        LINE_RESET,
        transfer(
            bytes([0x02]),                  # read DP IDCODE
            w(0x04, 0x54000000),            # CTRL/STAT: debug + system power up
            w(0x08, 0x00000000),            # SELECT AP0 bank 0
            w(0x01, 0x23000002),            # CSW: 32-bit
            w(0x05, 0x40030014),            # TAR = TEST_MODE
            w(0x0D, 0x80000000),            # DRW: TEST_MODE = 1 (stay in ROM)
        ),
    ]
    LOG.write(f"-> pipelined d={d_us}\n"); LOG.flush()
    for p in pkts:
        dev.write(EP_OUT, p, 500)
    replies = [bytes(dev.read(EP_IN, 512, 500)) for _ in pkts]
    tr = replies[-1]
    LOG.write(f"<- {tr[:12].hex()}\n"); LOG.flush()
    if not tr or tr[0] != 0x05:
        return False, None
    count, ack = tr[1], tr[2]
    idcode = struct.unpack("<I", tr[3:7])[0] if len(tr) >= 7 else None
    return (count == 6 and ack == 1), idcode


ok = False
for rnd in range(3):
    for d_us in list(range(0, 400, 20)) + list(range(400, 3000, 100)):
        good, idc = try_acquire(d_us)
        if good:
            print(f"connected at {d_us} us after reset, IDCODE=0x{idc:08X}")
            ok = True
            break
    if ok:
        break
if not ok:
    sys.exit("no acquire window found")


def rd(addr):
    r = xfer(transfer(w(0x05, addr), bytes([0x0F])))
    if r[1] != 2 or r[2] != 1:
        raise IOError(f"read 0x{addr:08X} failed: {r[:3].hex()}")
    return struct.unpack("<I", r[3:7])[0]


def wr(addr, val):
    r = xfer(transfer(w(0x05, addr), w(0x0D, val)))
    if r[1] != 2 or r[2] != 1:
        raise IOError(f"write 0x{addr:08X} failed: {r[:3].hex()}")


time.sleep(0.01)  # let the boot ROM settle into test mode


def recover_link():
    xfer(transfer(w(0x00, 0x0000001E),        # ABORT: clear sticky errors
                  w(0x04, 0x54000000),        # CTRL/STAT power up
                  w(0x08, 0x00000000),        # SELECT AP0
                  w(0x01, 0x23000002)), "recover")


def retry(fn, *a):
    last = None
    for _ in range(20):
        try:
            return fn(*a)
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(0.005)
            try:
                recover_link()
            except Exception:
                pass
    raise last


recover_link()
retry(wr, 0xE000EDF0, 0xA05F0003)          # halt CPU
dhcsr = retry(rd, 0xE000EDF0)
retry(wr, 0x40030038, 0xACED8865)          # watchdog off
hsiom3 = retry(rd, 0x40020300)
pc_hint = retry(rd, 0x40030014)            # TEST_MODE readback
print(f"DHCSR=0x{dhcsr:08X} halted={bool(dhcsr & (1 << 17))} TEST_MODE=0x{pc_hint:08X}")
print(f"HSIOM_PORT_SEL3 at boot = 0x{hsiom3:08X}  (pin2 nibble={hsiom3 >> 8 & 0xF:X}, pin3 nibble={hsiom3 >> 12 & 0xF:X})")
usb.util.release_interface(dev, 0)
print("ACQUIRED")
