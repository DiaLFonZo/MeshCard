"""SWD link test for the U115: halt the chip, turn off its watchdog, then read
memory over and over for N seconds. Counts link failures and chip restarts.
Read-only apart from the watchdog-off write."""
import sys
import time

from pyocd.core.helpers import ConnectHelper

TARGET = "cy8c4147azi-s475"
WDT_DISABLE_KEY_REG = 0x40030038
WDT_KEY = 0xACED8865
DHCSR = 0xE000EDF0
S_RESET_ST = 1 << 25
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 30
FREQ = int(sys.argv[2]) if len(sys.argv) > 2 else 400000

with ConnectHelper.session_with_chosen_probe(
        target_override=TARGET, connect_mode="attach", frequency=FREQ,
        options={"resume_on_disconnect": False}) as session:
    t = session.target
    t.halt()
    t.write32(WDT_DISABLE_KEY_REG, WDT_KEY)
    t.read32(DHCSR)  # clears sticky reset bit
    reads = fails = resets = 0
    end = time.time() + SECONDS
    while time.time() < end:
        try:
            t.read_memory_block32(0x20000000, 64)
            d = t.read32(DHCSR)
            reads += 1
            if d & S_RESET_ST:
                resets += 1
                t.halt()
                t.write32(WDT_DISABLE_KEY_REG, WDT_KEY)
        except Exception as e:  # link failure
            fails += 1
            time.sleep(0.05)
    print(f"freq={FREQ} reads={reads} link_failures={fails} chip_restarts={resets}")
