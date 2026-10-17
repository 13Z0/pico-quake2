#!/bin/bash
# Per-map budget sweep: loads each map with a cold flash asset cache, runs some frames
# of gameplay, and reports what it needs.
#
#   hosttest/sweep.sh <basedir> [frames] [maps...]      (default: demo1 demo2 demo3)
#
# Output: CSV on stdout. PSRAM figures include the engine's static data; the target is
# a peak of at most 7680 KB (7.5 MB of the 8 MB chip).
HERE="$(cd "$(dirname "$0")" && pwd)"
BASEDIR="${1:?usage: sweep.sh <basedir> [frames] [maps...]}"
FRAMES="${2:-300}"
shift 2 2>/dev/null
MAPS="${*:-demo1 demo2 demo3}"
FLASH="$(mktemp)"
trap 'rm -f "$FLASH"' EXIT

echo "map,static_kb,heap_peak_kb,psram_peak_kb,flash_cache_kb,flash_assets,first_visit_flash_s"
for map in $MAPS; do
	rm -f "$FLASH"
	out=$(Q2_FLASH_FILE="$FLASH" "$HERE/q2_host" "$BASEDIR" "$FRAMES" 0 /tmp/q2sweep +map "$map" 2>&1)
	static=$(echo "$out" | sed -n 's/.*PSRAM model: \([0-9]*\) KB static.*/\1/p')
	heap=$(echo "$out" | sed -n 's/.*heap peak \([0-9]*\) KB.*/\1/p')
	peak=$(echo "$out" | sed -n 's/.*PSRAM peak \([0-9]*\) KB.*/\1/p')
	assets=$(echo "$out" | sed -n 's/.*flash cache: \([0-9]*\) assets.*/\1/p')
	cache=$(echo "$out" | sed -n 's/.*flash cache: [0-9]* assets, \([0-9]*\) KB used.*/\1/p')
	secs=$(echo "$out" | sed -n 's/.*programmed, ~\([0-9.]*\) s on the device.*/\1/p')
	echo "$map,${static:-?},${heap:-FAILED},${peak:-?},${cache:-?},${assets:-?},${secs:-?}"
done
