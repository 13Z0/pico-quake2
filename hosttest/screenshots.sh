#!/bin/bash
# Render the README screenshots (docs/screenshots/) with the host harness.
#
#   hosttest/screenshots.sh <basedir>      <basedir>/baseq2/pak0.pak: the shareware demo
#
# The harness runs the firmware's engine and renderer. The runs set the two defaults
# from QG_PlatformConfig (src/pico/q2_input.c) that change the picture, screen size 70
# and brightness (vid_gamma) 0.7, and ppm2png.py --hdmi applies the board's RGB555
# palette and pixel doubling, so the PNGs show what the board puts on HDMI.
# The harness clock is virtual, so the frame numbers below always pick the same scenes
# (as long as the engine's behaviour does not change).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../docs/screenshots"
BASE="$1"

if [ ! -f "$BASE/baseq2/pak0.pak" ]; then
    echo "usage: $0 <basedir containing baseq2/pak0.pak>" >&2
    exit 1
fi
[ -x "$HERE/q2_host" ] || "$HERE/build.sh"
mkdir -p "$OUT"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# A scratch basedir, so the config.cfg and saves the runs write stay out of <basedir>.
mkdir -p "$TMP/base/baseq2"
ln -s "$(realpath "$BASE/baseq2/pak0.pak")" "$TMP/base/baseq2/pak0.pak"
if [ -d "$BASE/baseq2/players" ]; then
    ln -s "$(realpath "$BASE/baseq2/players")" "$TMP/base/baseq2/players"
fi

# run <name> <frames> <dump-every-N> [harness commands...]
run() {
    local name=$1 frames=$2 every=$3
    shift 3
    mkdir -p "$TMP/$name"
    if ! "$HERE/q2_host" "$TMP/base" "$frames" "$every" "$TMP/$name" \
            +set viewsize 70 +set vid_gamma 0.7 "$@" > "$TMP/$name.log" 2>&1; then
        tail -20 "$TMP/$name.log" >&2
        exit 1
    fi
}

# shot <run> <frame> <file>
shot() {
    local ppm="$TMP/$1/frame_$(printf %05d "$2").ppm"
    python3 -I "$HERE/ppm2png.py" --hdmi "$ppm" > /dev/null
    mv "${ppm%.ppm}.png" "$OUT/$3"
    echo "$OUT/$3"
}

# The attract demo (q2demo1.dm2), which plays through demo1 and demo2
run attract 3500 20
shot attract 580  demo1-firefight.png
shot attract 1180 demo1-warehouse.png
shot attract 2840 demo2-corridor.png
shot attract 3420 demo2-firefight.png

# The main menu over the attract demo
run menu 1360 1300 @1300 menu_main
shot menu 1300 main-menu.png

# demo3 with the help computer (F1, or the left trigger)
run demo3 600 25 @5 "map demo3" @400 "cmd help"
shot demo3 425 demo3-help-computer.png
