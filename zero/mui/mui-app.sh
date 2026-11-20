#!/bin/sh
# Launcher entry "Meshtastic UI": the Meshtastic touch UI (T-Deck layout) on the Zero.
# MeshZero's radio is paused while this runs and comes back on exit (long ESC).
# Same node and channels: MeshZero's saved settings are copied in, and back out on exit.
# Optional: MUI_STRETCH=1 fills the whole screen (squashed) instead of keeping the shape.
HERE=/home/pi/u115/mui
DATA=$HERE/data
MZ=/var/lib/meshzero/radio
LOG=$HERE/mui.log

echo pi | sudo -S -p '' systemctl stop meshzero 2>/dev/null
mkdir -p "$DATA"
if echo pi | sudo -S -p '' test -d "$MZ/prefs"; then
    echo pi | sudo -S -p '' cp -r "$MZ/prefs" "$DATA/"
    echo pi | sudo -S -p '' chown -R pi:pi "$DATA"
fi
echo 1 > /sys/class/leds/grove_5v_out/brightness 2>/dev/null
sleep 2

MUI_DUMP=/tmp/mui_frame.raw U115_PORT=/dev/serial0 /home/pi/u115/mui/meshtasticd-mui -c "$HERE/config.yaml" -d "$DATA" -p 4403 >"$LOG" 2>&1 &
PID=$!

finish() {
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    if [ -d "$DATA/prefs" ]; then
        echo pi | sudo -S -p '' cp -r "$DATA/prefs" "$MZ/"
        echo pi | sudo -S -p '' chown -R meshzero:meshzero "$MZ"
    fi
    echo pi | sudo -S -p '' systemctl start meshzero 2>/dev/null
    exit 0
}
trap finish TERM INT HUP
wait "$PID"
finish
