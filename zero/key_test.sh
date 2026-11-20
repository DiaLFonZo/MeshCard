#!/bin/sh
# Test without hands: run the screen app, send keys ($1, e.g. c,c,z,x), save the LCD after each.
cd /home/pi/u115
for p in $(pgrep -f meshtastic_screen.py); do [ "$p" != "$$" ] && kill "$p" 2>/dev/null; done
sleep 1
rm -f /tmp/t*.png
MS_TEST_KEYS="$1" timeout 16 python3 -u meshtastic_screen.py >/dev/null 2>/tmp/ms.err </dev/null &
for i in 0 1 2 3 4 5; do sleep 2; python3 fb_snap.py /tmp/t$i.png; done
sleep 4
journalctl -u meshtasticd --since -20s --no-pager | grep -e Injecting | cut -c60-200
