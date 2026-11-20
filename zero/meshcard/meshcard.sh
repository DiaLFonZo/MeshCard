#!/bin/sh
# Launcher entry "MeshCard": the MeshCard UI (MeshCard look, MeshZero inside).
# Uses MeshZero's radio service as is; only the screen app differs.
HERE=/home/pi/meshcard
export MESHZERO_FONT_DIR="$HERE/fonts"
exec "$HERE/meshcard" "$@"
