#!/bin/sh
# Radio start-up lines since the last service start, plus the meshtasticd config.
journalctl -u meshtasticd -b --no-pager | tail -n 3000 > /tmp/md_all.log
grep -i -e sx126 -e lora -e radio -e tcxo -e 'init result' -e u115 -e bridge /tmp/md_all.log | tail -30 | cut -c60-260
echo "--- config.yaml:"
grep -v -e '^ *#' -e '^ *$' /etc/meshtasticd/config.yaml
echo "--- config.d:"
for f in /etc/meshtasticd/config.d/*; do echo "== $f"; grep -v -e '^ *#' -e '^ *$' "$f"; done
