#!/bin/sh
# Turn on debug logging, restart meshtasticd, send one text from the Zero, show the radio's TX/RX lines.
printf 'Logging:\n  LogLevel: debug\n' > /tmp/zz-debug.yaml
echo pi | sudo -S -p '' cp /tmp/zz-debug.yaml /etc/meshtasticd/config.d/zz-debug.yaml
echo pi | sudo -S -p '' systemctl restart meshtasticd
sleep 20
T0=$(date '+%Y-%m-%d %H:%M:%S')
timeout 40 meshtastic --host 127.0.0.1 --sendtext "zero tx test" >/tmp/cli.out 2>&1
echo "cli: $(tail -2 /tmp/cli.out | tr '\n' ' ')"
sleep 20
journalctl -u meshtasticd --since "$T0" --no-pager > /tmp/tx.log
grep -i -e "tx" -e "transmit" -e "irq" -e "dio1" -e "rx" -e "sx126" -e "bridge" -e "error" -e "timeout" -e "rssi" -e "snr" /tmp/tx.log | grep -v -e U115\ key | cut -c60-240 | head -60
