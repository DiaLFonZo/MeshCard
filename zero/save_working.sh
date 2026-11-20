#!/bin/sh
# Snapshot the working Meshtastic setup on the Zero into ~/u115/saved/<name>.
D=/home/pi/u115/saved/${1:-working}
mkdir -p "$D"
cp /usr/local/bin/meshtasticd "$D/"
cp -r /etc/meshtasticd "$D/etc-meshtasticd"
cp /etc/systemd/system/meshtasticd.service "$D/" 2>/dev/null
cp -r /home/pi/.meshtasticd "$D/meshtasticd-data" 2>/dev/null
cp /home/pi/u115/*.py /home/pi/u115/*.sh /home/pi/u115/*.desktop "$D/" 2>/dev/null
du -sh "$D"
