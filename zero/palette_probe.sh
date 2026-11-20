#!/bin/sh
# Run the screen app in debug mode for 8 s and show whether colour info (palette, tag 22) arrives.
cd /home/pi/u115
for p in $(pgrep -f meshtastic_screen.py); do [ "$p" != "$$" ] && kill "$p" 2>/dev/null; done
sleep 1
MS_DEBUG=1 timeout 8 python3 -u meshtastic_screen.py >/dev/null 2>/tmp/ms.err </dev/null
echo "--- tags seen:"
grep -e '^rx' /tmp/ms.err | sort | uniq -c | sort -rn | head -20
echo "--- frame sample:"
grep -e '^frame' /tmp/ms.err | head -3
