#!/bin/sh
# Type "hi" on the home screen with the real keyboard path, snapshot, then put the screen app back.
sh /home/pi/u115/key_test.sh h,i
sleep 2
setsid nohup /usr/bin/python3 /home/pi/u115/meshtastic_screen.py >/dev/null 2>&1 </dev/null &
sleep 1
echo "screen app pid: $(pgrep -f meshtastic_screen.py | head -1)"
