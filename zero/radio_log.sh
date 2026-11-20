#!/bin/sh
# Radio-level lines of the last N seconds (default 60), without API/key noise.
journalctl -u meshtasticd --since "-${1:-60}s" --no-pager | grep -v -e ServerAPI -e U115\ key -e "Tell client" -e display -e Screen -e Telemetry -e Power | cut -c60-240 | tail -60
