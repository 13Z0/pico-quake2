#!/bin/bash
# Samples core0's PC over the probe and prints where the time goes, by function.
#
#   tools/pcsample.sh <elf> [samples]
#
# OpenOCD reads DWT_PCSR (the PC sample register) repeatedly while the core keeps
# running; each PC is mapped to its function with addr2line (inlined code is named
# after the inlined function). Run it while the board does something repeatable,
# e.g. tools/timedemo.sh. The raw PCs are kept in /tmp/pcsample.txt for a closer
# look at single instructions.
#
# (OpenOCD's own `profile` command writes a gmon histogram with 16-bit buckets,
# which overflow on the hottest loops and silently lose a quarter of the samples.)
ELF="${1:?usage: pcsample.sh <elf> [samples]}"
N="${2:-4000}"
PCS=/tmp/pcsample.txt

timeout 600 openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 4000" -c "init" \
	-c "set l {}" -c "for {set i 0} {\$i < $N} {incr i} { lappend l [read_memory 0xe000101c 32 1] }" \
	-c "echo \"PCS \$l\"" -c "exit" 2>&1 | grep "^PCS" | tr ' ' '\n' | grep "^0x" > "$PCS"
[ -s "$PCS" ] || { echo "pcsample.sh: no samples" >&2; exit 1; }

python3 - "$ELF" "$PCS" <<'PY'
import sys, subprocess, collections
elf, path = sys.argv[1], sys.argv[2]
pcs = [int(l, 16) for l in open(path)]
uniq = sorted(set(pcs))
out = subprocess.run(['arm-none-eabi-addr2line', '-f', '-C', '-e', elf] + [hex(p) for p in uniq],
                     capture_output=True, text=True).stdout.split('\n')
name = {p: out[2 * i] for i, p in enumerate(uniq)}
region = lambda p: {0x00: 'rom', 0x10: 'flash', 0x20: 'sram', 0x11: 'psram'}.get(p >> 24, 'other')
c = collections.Counter(name[p] if name[p] != '??' else '?' + region(p) for p in pcs)
r = collections.Counter(region(p) for p in pcs)
print(len(pcs), 'samples;', ', '.join('%s %.1f%%' % (k, 100 * v / len(pcs)) for k, v in r.most_common()))
for k, v in c.most_common(40):
    print('%5.1f%%  %s' % (100 * v / len(pcs), k))
PY
