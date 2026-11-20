#!/bin/bash
# Build MeshCard for the Cardputer Zero (arm64) on the PC, in WSL Debian (~2 min, no emulation).
# Needs: apt install g++-aarch64-linux-gnu cmake
# Output: out/zero/meshcard-arm64 and out/zero/meshcard-alertd-arm64 (copy to the Zero with tools/zero_put.py)
set -e
BASE="$(cd "$(dirname "$0")/.." && pwd)"
W=/root/meshcard
mkdir -p $W
cp -ru --preserve=timestamps "$BASE/meshcard/." $W/; cd $W
if [ ! -f build/arm/CMakeCache.txt ]; then
  cmake -S . -B build/arm -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DMESHZERO_BUILD_TESTS=OFF >/tmp/mc_arm_cmake.log 2>&1 || { tail -20 /tmp/mc_arm_cmake.log; exit 1; }
fi
cmake --build build/arm --target meshzero meshcard-alertd -j 8 2>&1 | grep -E "error|Built target" | head -20
OUT="$BASE/out/zero"; mkdir -p "$OUT"
aarch64-linux-gnu-strip -o "$OUT/meshcard-arm64" build/arm/meshzero
aarch64-linux-gnu-strip -o "$OUT/meshcard-alertd-arm64" build/arm/meshcard-alertd
ls -la "$OUT"
