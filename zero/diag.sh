#!/bin/sh
# Quick health check: service state, who is connected to the API port, recent log lines.
echo "--- service: $(systemctl is-active meshtasticd)"
echo "--- api connections (port 4403):"
ss -tn state established '( sport = :4403 )' 2>/dev/null
echo "--- screen app running:"
pgrep -af meshtastic_screen.py
echo "--- wifi:"
ip -4 addr show wlan0 | grep -e inet
echo "--- log (last 3 min, errors/connections):"
journalctl -u meshtasticd --since -3min --no-pager | grep -i -e error -e fault -e crash -e client -e "api" -e restart | tail -25
