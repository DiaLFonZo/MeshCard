#!/bin/bash
# Rebuild meshtasticd (native-u115, arm64) in the WSL chroot after copying changed sources.
# Usage (from Windows): wsl -d Debian -u root -- bash "/mnt/c/00 - GitHub/MeshCard/tools/wsl_rebuild.sh" src/path1 src/path2 ...
set -e
SRC="/mnt/c/00 - GitHub/MeshCard/meshtastic-firmware"
DST=/opt/zero/root/u115/meshtastic-firmware
for f in "$@"; do cp "$SRC/$f" "$DST/$f"; echo "copied $f"; done
mountpoint -q /opt/zero/proc || mount --bind /proc /opt/zero/proc
mountpoint -q /opt/zero/dev || mount --bind /dev /opt/zero/dev
set +e
chroot /opt/zero bash -c "cd /root/u115/meshtastic-firmware && /root/pio-venv/bin/pio run -e native-u115 -j 16" > /root/build2.log 2>&1
rc=$?
tail -5 /root/build2.log
[ $rc = 0 ] && cp $DST/.pio/build/native-u115/meshtasticd /root/meshtasticd-arm64 && strip --version >/dev/null 2>&1
echo rc=$rc
exit $rc
