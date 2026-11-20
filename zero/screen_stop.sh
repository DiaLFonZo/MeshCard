#!/bin/sh
# Stop every copy of the Meshtastic screen app (not this shell), so the Zero's launcher owns the LCD again.
for p in $(pgrep -f 'python3 /home/pi/u115/meshtastic_screen.py'); do
  [ "$p" != "$$" ] && kill "$p" 2>/dev/null
done
sleep 2
echo "left running: $(pgrep -fc 'python3 /home/pi/u115/meshtastic_screen.py')"
