#!/bin/sh
# Launcher entry for "Meshtastic": shows Meshtastic's own screen (from the meshtasticd
# service) on the Zero's LCD and sends the keyboard to it. Long ESC quits.
exec /usr/bin/python3 /home/pi/u115/meshtastic_screen.py
