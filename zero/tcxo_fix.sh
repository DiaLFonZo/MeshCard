#!/bin/sh
# The ASR6501 (Ra-07H) SX1262 runs from a TCXO powered by DIO3 at 1.8 V (CubeCell core always
# builds with CONFIG_LORA_USE_TCXO). Add it to the radio config, restart, show radio init.
F=/etc/meshtasticd/config.d/lora-u115.yaml
if ! grep -q -e '^  DIO3_TCXO_VOLTAGE' $F; then
  cp $F /tmp/lora-u115.yaml
  printf '  DIO3_TCXO_VOLTAGE: 1.8\n' >> /tmp/lora-u115.yaml
  [ -f $F.bak-notcxo ] || echo pi | sudo -S -p '' cp $F $F.bak-notcxo
  echo pi | sudo -S -p '' cp /tmp/lora-u115.yaml $F
fi
tail -3 $F
echo pi | sudo -S -p '' systemctl restart meshtasticd
sleep 20
echo "--- service: $(systemctl is-active meshtasticd)"
journalctl -u meshtasticd --since -25s --no-pager | grep -i -e tcxo -e sx126 -e radio -e error -e fault -e fail | cut -c60-240
