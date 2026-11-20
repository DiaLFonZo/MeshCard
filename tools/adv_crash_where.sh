#!/bin/bash
# Turn crash addresses (from the ADV log or the "crash at 0x..." restart note) into file:line.
# Usage (WSL): bash tools/adv_crash_where.sh 0x4202b426 0x420870df ...
# Uses the .elf from the last tools/adv_build.sh run.
A=$(find /root/.platformio/packages -name '*elf-addr2line' 2>/dev/null | grep -i -E 'esp32s3|xtensa' | head -1)
ELF=$(ls /root/fw-adv/.pio/build/m5stack-cardputer-adv-meshcard/firmware-*.elf | head -1)
$A -pfiaC -e "$ELF" "$@"
