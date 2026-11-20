#!/bin/sh
# Radio-level lines (no API/key noise) from the current debug log, plus a test send with python's meshtastic module.
T0=$(date '+%Y-%m-%d %H:%M:%S')
timeout 40 python3 -m meshtastic --host 127.0.0.1 --sendtext "zero tx test" >/tmp/cli.out 2>&1
echo "cli rc=$? : $(tail -2 /tmp/cli.out | tr '\n' ' ')"
sleep 15
journalctl -u meshtasticd --since -4min --no-pager | grep -v -e ServerAPI -e U115\ key -e "Tell client" -e "display" -e "Screen" > /tmp/rx.log
grep -i -e preamble -e header -e crc -e "lora" -e "radio" -e "tx" -e "rx" -e irq -e "send" -e "airtime" -e "channel" /tmp/rx.log | cut -c60-240 | tail -50
