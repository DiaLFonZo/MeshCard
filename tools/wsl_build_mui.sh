#!/bin/bash
# Build meshtasticd with the Meshtastic UI (env native-u115-mui) in the WSL chroot.
# Copies our device-ui and the changed firmware files first.
set -e
BASE="/mnt/c/00 - GitHub/MeshCard"
DST=/opt/zero/root/u115
mkdir -p $DST/device-ui
cp -ru --preserve=timestamps "$BASE/device-ui/." $DST/device-ui/  # keep dates: a fresh copy forces a full 1-hour rebuild
for f in src/graphics/tftSetup.cpp variants/native/portduino/platformio.ini "$@"; do
  cp -u --preserve=timestamps "$BASE/meshtastic-firmware/$f" "$DST/meshtastic-firmware/$f"; echo "copied $f"; done
mountpoint -q /opt/zero/proc || mount --bind /proc /opt/zero/proc
mountpoint -q /opt/zero/dev || mount --bind /dev /opt/zero/dev
set +e
chroot /opt/zero bash -c "cd /root/u115/meshtastic-firmware && /root/pio-venv/bin/pio run -e native-u115-mui -j 16" > /root/build-mui.log 2>&1
rc=$?
tail -25 /root/build-mui.log
if [ $rc = 0 ]; then
  chroot /opt/zero strip -o /root/meshtasticd-mui /root/u115/meshtastic-firmware/.pio/build/native-u115-mui/meshtasticd
  cp /opt/zero/root/meshtasticd-mui /mnt/c/Users/enant/tools/u115-fw/meshtasticd-mui-arm64
fi
echo rc=$rc
exit $rc
