#!/bin/bash
# PC preview: build MeshCard for the PC (headless) and take screenshots, in WSL Debian.
# Usage: bash tools/preview_build.sh <shots list file> [adv|zero]
#   adv  = 240x135 (Cardputer ADV), zero = 320x170 (Cardputer Zero, default)
# The shots list is the key-script format read by meshcard/tools/uitest.sh.
# Output: out/shots-<size>/*.png
set -e
BASE="$(cd "$(dirname "$0")/.." && pwd)"
W=/root/meshcard
SIZE=${2:-zero}
[ -f "$BASE/meshcard/CMakeLists.txt" ] || { echo "Initialize meshcard with git submodule update --init" >&2; exit 1; }
mkdir -p $W
rsync -a --delete --exclude '/.git' --exclude 'build*/' "$BASE/meshcard/" "$W/"
cd $W
sed -i "s/\r$//" tools/*.sh tools/*.py
[ -d /root/mcvenv ] || { python3 -m venv /root/mcvenv && /root/mcvenv/bin/pip -q install meshtastic; }
if [ "$SIZE" = "adv" ]; then B=build/host-adv; X="-DMC_SCREEN_W=240 -DMC_SCREEN_H=135"; else B=build/host; X=""; fi
cmake -S . -B $B -DCMAKE_BUILD_TYPE=Release $X >/tmp/mc_cmake.log 2>&1 || { tail -20 /tmp/mc_cmake.log; exit 1; }
cmake --build $B -j 8 2>&1 | grep -E "error|Built target meshzero$" | head -30
mkdir -p shots && rm -f shots/*
tr -d '\r' < "$1" > /root/shots.txt
BUILD=$W/$B PYTHON=/root/mcvenv/bin/python sh tools/uitest.sh /root/shots.txt 2>&1 | grep -v tiny_ttf | tail -3
D="$BASE/out/shots-$SIZE"; mkdir -p "$D"; rm -f "$D"/*; cp shots/* "$D/"; ls "$D"
