#!/bin/sh
# Install MeshZero on the Cardputer Zero with the U115 as its radio. Run with sudo.
# Undo: zero/meshzero/uninstall.sh (puts our meshtasticd service back).
set -e
HERE=/home/pi/u115/meshzero

echo "== stop our meshtasticd service and screen app"
systemctl stop meshtasticd || true
systemctl disable meshtasticd || true
for p in $(pgrep -f 'python3 /home/pi/u115/meshtastic_screen.py'); do kill "$p" 2>/dev/null || true; done

echo "== settings first, so MeshZero starts on the U115 straight away"
mkdir -p /etc/meshzero
cp "$HERE/radio.yaml" /etc/meshzero/radio.yaml
cp "$HERE/meshzero.conf" /etc/meshzero/meshzero.conf
printf 'z /sys/class/leds/grove_5v_out/brightness 0664 root gpio - -\n' > /etc/tmpfiles.d/meshzero-u115.conf
systemd-tmpfiles --create /etc/tmpfiles.d/meshzero-u115.conf || true
ln -sf /usr/local/bin/meshtasticd /usr/bin/meshtasticd

echo "== keep the node identity and channels"
mkdir -p /var/lib/meshzero/radio
cp -r /home/pi/.meshtasticd/prefs /var/lib/meshzero/radio/

echo "== install the package"
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  -o Dpkg::Options::=--force-confold -o Dpkg::Options::=--force-confdef "$HERE/meshzero_0.2.0_arm64.deb"

chown -R meshzero:meshzero /var/lib/meshzero
chmod 0700 /var/lib/meshzero/radio
systemctl restart meshzero
sleep 15
echo "== status"
systemctl is-active meshzero
journalctl -u meshzero --since -40s --no-pager | tail -30 | cut -c1-220
