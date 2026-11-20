"""Flash a .hex onto the U115 (ASR6501 / PSoC4) over a flaky SWD link.

Writes one 256-byte flash row at a time, reads it back, and retries the row
(reconnecting if needed) until it matches. Rows already correct are skipped,
so the script can simply be run again if it ever stops.

Usage: python flash_robust.py firmware.hex [freq_hz]
"""
import sys
import time

from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target

TARGET = "cy8c4147azi-s475"
FLASH_END = 0x20000
ROW = 0x100
WDT_DISABLE_KEY_REG = 0x40030038
WDT_KEY = 0xACED8865
MAX_TRIES = 30

hex_path = sys.argv[1]
freq = int(sys.argv[2]) if len(sys.argv) > 2 else 400000

ih = IntelHex(hex_path)
ih.padding = 0x00  # PSoC4 erased flash reads as 0x00, so fill gaps with 0x00
rows = {}
for start, stop in ih.segments():
    if start >= FLASH_END:
        continue  # PSoC config/protection areas: leave untouched
    for addr in range(start - start % ROW, min(stop, FLASH_END), ROW):
        rows[addr] = bytes(ih.tobinarray(start=addr, size=ROW))
order = sorted(rows)
print(f"{len(order)} rows to check ({len(order) * ROW // 1024} KB)")


def prepare(session):
    t = session.target
    t.halt()
    t.write32(WDT_DISABLE_KEY_REG, WDT_KEY)
    flash = t.memory_map.get_boot_memory().flash
    flash.init(flash.Operation.PROGRAM)
    return t, flash


def read_row(t, addr):
    return bytes(t.read_memory_block8(addr, ROW))


done = skipped = retries = 0
with ConnectHelper.session_with_chosen_probe(
        target_override=TARGET, connect_mode="attach", frequency=freq,
        # pack debug sequences reset the chip, which lets firmware that turns
        # off SWD run; keep them off (chip stays as psoc4_acquire.py left it)
        options={"resume_on_disconnect": False,
                 "pack.debug_sequences.enable": False}) as session:
    for attempt in range(MAX_TRIES):
        try:
            t, flash = prepare(session)
            break
        except Exception:
            time.sleep(0.1)
    else:
        sys.exit("could not start the flash helper on the chip")
    for addr in order:
        want = rows[addr]
        for attempt in range(MAX_TRIES):
            try:
                if read_row(t, addr) == want:
                    if attempt == 0:
                        skipped += 1
                    else:
                        done += 1
                    break
                flash.program_page(addr, list(want))
            except Exception:
                retries += 1
                time.sleep(0.05)
                try:
                    t, flash = prepare(session)
                except Exception:
                    time.sleep(0.2)
        else:
            print(f"FAILED at row 0x{addr:05X} after {MAX_TRIES} tries")
            sys.exit(1)
        n = done + skipped
        if n % 32 == 0:
            print(f"  {n}/{len(order)} rows ok (retries so far: {retries})", flush=True)

    # final full verify
    bad = []
    for addr in order:
        for _ in range(5):
            try:
                if read_row(t, addr) != rows[addr]:
                    bad.append(addr)
                break
            except Exception:
                time.sleep(0.05)
    print(f"written={done} already-ok={skipped} retries={retries} verify_bad_rows={len(bad)}")
    if bad:
        print("BAD:", [hex(a) for a in bad[:10]])
        sys.exit(1)
    try:
        flash.cleanup()
    except Exception:
        pass
    print("FLASH OK")
