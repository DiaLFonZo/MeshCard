#!/bin/bash
D=/opt/zero/root/u115/meshtastic-firmware/.pio/libdeps/native-u115/RadioLib/src/modules/SX126x
grep -n -A50 'int16_t SX126x::setTCXO' $D/SX126x_config.cpp
echo "=== begin / reset / standby"
grep -n -B2 -A30 'int16_t SX126x::begin(' $D/SX126x.cpp | head -60
