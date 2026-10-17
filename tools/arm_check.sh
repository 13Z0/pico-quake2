#!/bin/bash
# Compiles the engine sources for the Cortex-M33 with the firmware's flags (no link)
# and reports what matters before the firmware build exists:
#   - files that do not compile
#   - stack frames larger than FRAME_LIMIT bytes (-fstack-usage)
#   - float-to-double promotions (-Wdouble-promotion), per file
#   - static data (.bss/.data/COMMON) in bytes, which goes to PSRAM on the device
#
#   tools/arm_check.sh [FRAME_LIMIT]          (default 1024)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
SRC="$ROOT/src/quake2"
OUT="${TMPDIR:-/tmp}/pico-quake2-armcheck"
LIMIT="${1:-1024}"
rm -rf "$OUT" && mkdir -p "$OUT"

# Same file list as hosttest/build.sh (the engine proper, no platform layer)
FILES=$(cd "$SRC" && ls client/*.c qcommon/*.c server/*.c game/*.c ref_soft/*.c \
	net/net_loopback.c null/cd_null.c other/q_hunk.c other/q_system.c other/vid_lib.c \
	other/vid_menu.c other/glob.c)

ARCH="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp"
CFLAGS="$ARCH -O2 -g -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv \
	-fno-aggressive-loop-optimizations -fcommon -fno-math-errno -fsingle-precision-constant -Dstricmp=strcasecmp \
	-fstack-usage -Wdouble-promotion -I$SRC -I$SRC/other -include q2_force.h \
	$EXTRA_CFLAGS"
export CFLAGS OUT SRC

compile() {
	o="$OUT/$(echo "$1" | tr / _ | sed 's/\.c$/.o/')"
	if ! arm-none-eabi-gcc $CFLAGS -c "$SRC/$1" -o "$o" 2> "${o%.o}.log"; then
		echo "FAILED: $1"; sed -n '1,5p' "${o%.o}.log"
	fi
}
export -f compile
echo $FILES | tr ' ' '\n' | xargs -P "$(nproc)" -I{} bash -c 'compile {}'
python3 -I "$ROOT/tools/gen_savetables.py" "$SRC/game" "$OUT/g_savetables.c" > /dev/null
arm-none-eabi-gcc $CFLAGS -I"$SRC/game" -c "$OUT/g_savetables.c" -o "$OUT/game_g_savetables.o" 2> "$OUT/game_g_savetables.log" ||
	{ echo "FAILED: g_savetables.c"; sed -n '1,5p' "$OUT/game_g_savetables.log"; }

echo "== stack frames > $LIMIT bytes"
cat "$OUT"/*.su | sed 's|^.*/src/quake2/||' | awk -F'\t' -v lim="$LIMIT" '$2 + 0 > lim + 0 {printf "%8d  %s\n", $2, $1}' |
	sort -rn

echo "== -Wdouble-promotion warnings per file"
grep -h "double-promotion" "$OUT"/*.log | sed 's|.*/src/quake2/||; s|:.*||' | sort | uniq -c | sort -rn | head -20
echo "total: $(grep -h "double-promotion" "$OUT"/*.log | wc -l)"

echo "== static data on the device (bytes)"
for o in "$OUT"/*.o; do
	arm-none-eabi-nm -S -t d "$o" 2>/dev/null | awk -v f="$(basename "$o")" '$3 ~ /^[bBdDcC]$/ {s+=$2} END {if (s) print s, f}'
done | sort -rn | awk '{t+=$1; if (NR <= 12) printf "%9d  %s\n", $1, $2} END {printf "%9d  TOTAL\n", t}'
