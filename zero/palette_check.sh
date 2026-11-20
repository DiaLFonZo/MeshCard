#!/bin/sh
# Count palette messages (tag 22) and frames carrying a palette signature (field 12) in the last debug run.
echo "palette msgs: $(grep -c -e 'rx \[22\]' /tmp/ms.err)"
grep -e '^frame' /tmp/ms.err | grep -e ' 12: ' | head -2
