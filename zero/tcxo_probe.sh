#!/bin/sh
# Pause meshtasticd, keep the U115 powered, run the clock probe, start meshtasticd again.
echo pi | sudo -S -p '' systemctl stop meshtasticd
echo pi | sudo -S -p '' sh -c 'echo 1 > /sys/class/leds/grove_5v_out/brightness'
sleep 2
python3 /home/pi/u115/tcxo_probe.py /dev/serial0
echo pi | sudo -S -p '' systemctl start meshtasticd
sleep 12
echo "service: $(systemctl is-active meshtasticd)"
