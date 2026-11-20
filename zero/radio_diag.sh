#!/bin/sh
# Radio health: region/preset, recent TX/RX, errors.
journalctl -u meshtasticd --since -15min --no-pager > /tmp/md.log
echo "--- region / preset / freq:"
grep -i -e "region" -e "frequency" -e "freq=" -e "Set radio" -e "LongFast" -e "Wanted region" /tmp/md.log | tail -8 | cut -c60-250
echo "--- tx / rx:"
grep -i -e "Started Tx" -e "Completed sending" -e "Lora RX" -e "Received" -e "rx_" -e "transmit" -e "airtime" /tmp/md.log | tail -15 | cut -c60-250
echo "--- errors:"
grep -i -e error -e fault -e fail -e timeout -e bridge /tmp/md.log | tail -15 | cut -c60-250
