#!/bin/sh
# Pause meshtasticd, listen on LongFast through the bridge, start meshtasticd again.
echo pi | sudo -S -p '' systemctl stop meshtasticd
echo pi | sudo -S -p '' sh -c 'echo 1 > /sys/class/leds/grove_5v_out/brightness'
sleep 2
RX_DUMP=/tmp/rx_dump.txt RX_MHZ=${3:-906.875} python3 /home/pi/u115/rx_probe.py /dev/serial0 ${1:-120} $2
echo pi | sudo -S -p '' systemctl start meshtasticd
sleep 10
echo "service: $(systemctl is-active meshtasticd)"
