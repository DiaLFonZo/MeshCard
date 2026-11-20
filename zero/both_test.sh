#!/bin/sh
# Start the screen app for 45 s (keys 20,20 to move pages), snapshot the LCD every 5 s.
# Run phone_sim.py from the PC meanwhile.
cd /home/pi/u115
for p in $(pgrep -f meshtastic_screen.py); do [ "$p" != "$$" ] && kill "$p" 2>/dev/null; done
sleep 1
rm -f /tmp/b*.png
MS_DEBUG=1 MS_TEST_KEYS="$1" timeout 45 python3 -u meshtastic_screen.py >/dev/null 2>/tmp/ms.err </dev/null &
for i in 0 1 2 3 4 5 6 7; do sleep 5; python3 fb_snap.py /tmp/b$i.png; done
echo "reconnects: $(grep -c -e reconnect /tmp/ms.err)"
echo "frames: $(grep -c -e '^frame' /tmp/ms.err)"
journalctl -u meshtasticd --since -50s --no-pager | grep -e "Incoming" -e "Force close" -e "Both API" -e "display mirror" -e "dropped" | cut -c60-200
