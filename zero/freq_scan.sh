#!/bin/sh
# Pause meshtasticd, run the frequency scan, start meshtasticd again.
echo pi | sudo -S -p '' systemctl stop meshtasticd
echo pi | sudo -S -p '' sh -c 'echo 1 > /sys/class/leds/grove_5v_out/brightness'
sleep 2
python3 /home/pi/u115/freq_scan.py /dev/serial0 ${1:-120} $2
echo pi | sudo -S -p '' systemctl start meshtasticd
sleep 10
echo "service: $(systemctl is-active meshtasticd)"
