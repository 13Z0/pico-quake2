#!/bin/bash
# Run q2_host with the live-block table and resolve the failure dump.
#   hosttest/memlive.sh <q2_host args...>
HERE="$(cd "$(dirname "$0")" && pwd)"
Q2_MEMLIVE=1 "$HERE/q2_host" "$@" 2>&1 | grep -a "^q_mem" | while read -r tag kb unit n a b c; do
	if [[ "$a" == 0x* ]]; then
		names=$(addr2line -f -e "$HERE/q2_host" "$a" "$b" "$c" | sed -n '1p;3p;5p' | tr '\n' ' ')
		echo "$kb KB x$n  $names"
	else
		echo "$tag $kb $unit $n $a $b $c"
	fi
done
