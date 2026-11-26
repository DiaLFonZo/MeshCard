#!/bin/bash
# PC preview: build MeshCard for the PC (headless) and take screenshots, in WSL Debian.
# Usage: bash tools/preview_build.sh <shots list file> [adv|zero]
#   adv  = 240x135 (Cardputer ADV), zero = 320x170 (Cardputer Zero, default)
# The shots list is the key-script format read by meshcard/tools/uitest.sh.
# Output: out/shots-<size>/*.png
set -euo pipefail
BASE="$(cd "$(dirname "$0")/.." && pwd)"
W=/root/meshcard
SIZE=${2:-zero}
[ -f "$BASE/meshcard/CMakeLists.txt" ] || { echo "Initialize meshcard with git submodule update --init" >&2; exit 1; }
mkdir -p $W
rsync -a --delete --exclude '/.git' --exclude 'build*/' "$BASE/meshcard/" "$W/"
cd $W
sed -i "s/\r$//" tools/*.sh tools/*.py
[ -d /root/mcvenv ] || { python3 -m venv /root/mcvenv && /root/mcvenv/bin/pip -q install meshtastic; }
case "$SIZE" in
  adv) B=build/host-adv; X=(-DMC_SCREEN_W=240 -DMC_SCREEN_H=135 -DMESHCARD_ZERO_MAP=OFF) ;;
  zero) B=build/host; X=(-DMC_SCREEN_W=320 -DMC_SCREEN_H=170 -DMESHCARD_ZERO_MAP=ON) ;;
  *) echo "Use adv or zero" >&2; exit 2 ;;
esac
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release "${X[@]}" >/tmp/mc_cmake.log 2>&1 || { tail -20 /tmp/mc_cmake.log; exit 1; }
cmake --build "$B" --target meshzero meshzerod -j 8 >/tmp/mc_preview_build.log 2>&1 || { tail -60 /tmp/mc_preview_build.log; exit 1; }
mkdir -p shots && rm -f shots/*
tr -d '\r' < "$1" > /root/shots.txt
BUILD=$W/$B PYTHON=/root/mcvenv/bin/python sh tools/uitest.sh /root/shots.txt --quiet-mesh >/tmp/mc_preview_test.log 2>&1 || { tail -40 /tmp/mc_preview_test.log; exit 1; }
D="$BASE/out/shots-$SIZE"; mkdir -p "$D"; rm -f "$D"/*; cp shots/* "$D/"; ls "$D"
