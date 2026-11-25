#!/bin/bash
# Build the Cardputer ADV firmware (U115 radio + MeshCard screens) in WSL Debian.
# From Windows:  wsl -d Debian -u root -- bash "/mnt/c/00 - GitHub/MeshCard/tools/adv_build.sh" [selftest] [env]
# Output: out/adv/firmware-*.bin  (factory.bin = first install at 0x0, the other = update at 0x10000)
# Builds in a Linux-side copy (/root/fw-adv) because building on /mnt/c is very slow.
set -e
BASE="$(cd "$(dirname "$0")/.." && pwd)"
W=/root/fw-adv
ENV=${2:-m5stack-cardputer-adv-meshcard}
[ -f "$BASE/meshcard/CMakeLists.txt" ] || { echo "Initialize meshcard with git submodule update --init" >&2; exit 1; }
mkdir -p $W /root/meshcard
rsync -a --exclude '.pio/' --exclude 'build/' "$BASE/meshtastic-firmware/" $W/
rsync -a --delete --exclude 'build*/' --exclude '/.git' "$BASE/meshcard/" /root/meshcard/
if [ "$1" = "selftest" ]; then
  echo "#define MC_SELFTEST 1" > /root/meshcard/src/ui/selftest_on.h; echo "SELFTEST BUILD"
else
  echo "// normal build" > /root/meshcard/src/ui/selftest_on.h
fi
cd $W
/root/pio-venv/bin/pio run -e $ENV -j 8 > /root/adv_build.log 2>&1 || {
  grep -E "error|Error|undefined reference" /root/adv_build.log | head -40; tail -15 /root/adv_build.log; exit 1; }
grep -E "RAM:|Flash:" /root/adv_build.log | tail -2
OUT="$BASE/out/adv"
mkdir -p "$OUT"
cp .pio/build/$ENV/firmware-*.bin "$OUT/"
cp .pio/build/$ENV/firmware-*.elf "$OUT/" 2>/dev/null || true
ls -la "$OUT"
