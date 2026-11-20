#!/bin/sh
# Read-only look before installing MeshZero: current radio config, I2C buses, files.
ls /etc/meshtasticd/config.d/
echo "--- config.yaml"; grep -v '^ *#' /etc/meshtasticd/config.yaml | grep -v '^$'
echo "--- lora-u115.yaml"; grep -v '^ *#' /etc/meshtasticd/config.d/lora-u115.yaml | grep -v '^$'
echo "--- devices"; ls -l /dev/serial0 /dev/i2c-* 2>&1
for b in /sys/bus/i2c/devices/i2c-*; do echo "$b $(cat $b/name)"; done
ls /sys/bus/i2c/devices/
echo "--- i2c-1 0x43 check"
if command -v i2cdetect >/dev/null; then echo pi | sudo -S -p '' i2cdetect -y -r 1 2>&1; else echo "no i2cdetect"; fi
echo "--- other"
ls -l /usr/bin/meshtasticd 2>&1
ls -la /home/pi/.meshtasticd 2>&1 | head
grep -e Exec -e User /etc/systemd/system/meshtasticd.service
ls -l /sys/class/leds/ | grep -i 5v
groups pi
