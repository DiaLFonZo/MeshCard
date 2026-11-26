#!/bin/bash
# Render the current MeshCard UI against the existing simulated mesh.
# Usage: bash tools/screenshots.sh [zero|adv] [output-directory]
# Set BUILD and optionally SIM_SOURCE and PYTHON to reuse a native build.
set -euo pipefail
BASE="$(cd "$(dirname "$0")/.." && pwd)"
mode=${1:-zero}
case "$mode" in
  zero|adv) ;;
  *) echo "Use zero or adv" >&2; exit 2 ;;
esac
destination=${2:-"$BASE/out/screenshots-$mode"}
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat > "$work/keys.txt" <<'EOF'
wait 16000
shot shots/home.png
key enter
wait 300
shot shots/chats.png
key enter
wait 300
type On my way, see you at 8
key enter
wait 900
shot shots/conversation.png
key esc tab
wait 300
shot shots/nodes.png
key down down enter
wait 300
shot shots/node-details.png
key esc tab
wait 350
shot shots/radar.png
EOF
if [ "$mode" = zero ]; then
  cat >> "$work/keys.txt" <<'EOF'
key m
wait 6500
shot shots/street.png
key m
wait 6500
shot shots/topo.png
EOF
fi
cat >> "$work/keys.txt" <<'EOF'
key tab
wait 300
shot shots/radio.png
key tab
wait 300
shot shots/settings.png
quit
EOF

if [ -n "${BUILD:-}" ]; then
  export BUILD="$(cd "$BUILD" && pwd)"
  source_dir=${SIM_SOURCE:-"$BASE/meshcard"}
  mkdir -p "$work/images"
  sed "s|^shot shots/|shot $work/images/|" "$work/keys.txt" > "$work/absolute-keys.txt"
  # A Windows checkout may give the upstream simulator runner CRLF endings.
  tr -d '\r' < "$source_dir/tools/uitest.sh" > "$work/uitest.sh"
  (cd "$source_dir/tools"; sh -s -- "$work/absolute-keys.txt" --quiet-mesh < "$work/uitest.sh") > "$work/simulator.log" 2>&1 ||
    { tail -40 "$work/simulator.log"; exit 1; }
  images="$work/images"
else
  bash "$BASE/tools/preview_build.sh" "$work/keys.txt" "$mode"
  images="$BASE/out/shots-$mode"
fi

mkdir -p "$destination"
while read -r command argument; do
  [ "$command" = shot ] || continue
  name=${argument##*/}
  [ -s "$images/$name" ] || { echo "Screenshot missing: $name" >&2; exit 1; }
  cp "$images/$name" "$destination/"
done < "$work/keys.txt"
echo "Screenshots saved to $destination"
