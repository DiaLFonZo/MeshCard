#!/bin/sh
# Go back to the saved working setup: remove MeshZero, re-enable our meshtasticd service. Run with sudo.
systemctl stop meshzero 2>/dev/null
apt-get remove -y meshzero
rm -f /usr/bin/meshtasticd /etc/tmpfiles.d/meshzero-u115.conf
systemctl enable meshtasticd
systemctl start meshtasticd
sleep 10
systemctl is-active meshtasticd
