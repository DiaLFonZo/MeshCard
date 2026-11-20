#!/bin/sh
ls /usr/lib/aarch64-linux-gnu/ | grep -E "libinput|xkbcommon|libcurl"
ls -l /dev/input/by-path/ /dev/input/by-id/ 2>&1
cat /proc/bus/input/devices | grep -E "^N|^H"
ls -l /dev/input/event*
