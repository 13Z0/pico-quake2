#!/bin/bash
# Sends console commands to the board over its UART and waits for a reply line.
#
#   tools/uart_cmd.sh <uart-log> <timeout-s> <pattern> <command>...
#
# Each <command> is sent as one console line. Waits until a line matching the
# extended regex <pattern> appears in <uart-log> after the commands were sent
# and prints it; exits 1 on timeout. The log is the file a capture of the
# board's UART (e.g. `cat /dev/ttyACM0 > uart.log`) is writing to.
LOG="${1:?usage: uart_cmd.sh <uart-log> <timeout-s> <pattern> <command>...}"
TMO="$2"
PAT="$3"
shift 3
TTY="${UART_TTY:-/dev/ttyACM0}"

start=$(stat -c %s "$LOG")
for c in "$@"; do
	printf '%s\n' "$c" > "$TTY"
	sleep 0.3
done
for i in $(seq "$TMO"); do
	hit=$(tail -c +$((start + 1)) "$LOG" | tr -d '\000\r' | grep -aE "$PAT" | head -1)
	[ -n "$hit" ] && { echo "$hit"; exit 0; }
	sleep 1
done
echo "uart_cmd.sh: no match for '$PAT' within $TMO s" >&2
exit 1
