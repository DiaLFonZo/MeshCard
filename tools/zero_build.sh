#!/bin/bash
# Build MeshCard for the Cardputer Zero (arm64) on the PC, in WSL Debian (~2 min, no emulation).
# Needs: apt install g++-aarch64-linux-gnu cmake rsync
# Output: out/zero/meshcard-arm64 and out/zero/meshcard-alertd-arm64 (copy to the Zero with tools/zero_put.py)
set -e
BASE="$(cd "$(dirname "$0")/.." && pwd)"
W=/root/meshcard
[ -f "$BASE/meshcard/CMakeLists.txt" ] || { echo "Initialize meshcard with git submodule update --init" >&2; exit 1; }
mkdir -p $W
rsync -a --delete --exclude '/.git' --exclude 'build*/' "$BASE/meshcard/" "$W/"
cd "$W"
if [ ! -f build/arm/CMakeCache.txt ]; then
  cmake -S . -B build/arm -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DMESHZERO_BUILD_TESTS=OFF >/tmp/mc_arm_cmake.log 2>&1 || { tail -20 /tmp/mc_arm_cmake.log; exit 1; }
fi
if ! cmake --build build/arm --target meshzero meshcard-alertd -j 8 > /tmp/mc_arm_build.log 2>&1; then
  tail -60 /tmp/mc_arm_build.log
  exit 1
fi
tail -20 /tmp/mc_arm_build.log
OUT="$BASE/out/zero"; mkdir -p "$OUT"
aarch64-linux-gnu-strip -o "$OUT/meshcard-arm64" build/arm/meshzero
aarch64-linux-gnu-strip -o "$OUT/meshcard-alertd-arm64" build/arm/meshcard-alertd
ls -la "$OUT"
