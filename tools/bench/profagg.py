#!/usr/bin/env python3
# profagg.py LOG NMFILE [N]: sums the per-address profile Hatari prints at
# the end of a "run.sh LABEL MODE prof" run (in its log, after "CPU
# profiling warning counts:", or in profile.txt) per function, with the nm symbols mkbuild.sh
# keeps (nm_LABEL.txt), and lists the top N: share of cycles, ms a frame
# (over the 600-tic demo at 32 MHz), instructions, cycles an instruction,
# instruction cache misses and data cache hits. The text segment's address
# comes from the profile.txt beside the log.
import sys, re, bisect, os
log, nmf = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 50
syms = []
for l in open(nmf):
    p = l.split()
    if len(p) == 3 and p[1] in 'TtW' and not p[2].endswith('.o') and not p[2].startswith('.L'):
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [a for a, _ in syms]
text = None
lines = open(log, errors='replace').read().splitlines()
base = None
for l in lines:
    m = re.search(r'PROGRAM_TEXT:\s*0x([0-9a-f]+)', l)
    if m: base = int(m.group(1), 16)
# the run log doesn't print PROGRAM_TEXT; take it from the profile file
if base is None:
    for l in open(os.path.join(os.path.dirname(log), 'profile.txt'), errors='replace'):
        m = re.search(r'PROGRAM_TEXT:\s*0x([0-9a-f]+)', l)
        if m: base = int(m.group(1), 16); break
pat = re.compile(r'^([0-9a-f]{8}) .*\s([0-9.]+)% \((\d+), (\d+), (\d+), (\d+)\)$')
tot = {}
# The per-address lines: in profile.txt (Hatari 2.6.1 as released), or, where
# the debugger prints them instead (the emumd fork), in the log's largest
# dump (breakpoints print empty ones too)
saved = [l.rstrip('\n') for l in open(os.path.join(os.path.dirname(log), 'profile.txt'), errors='replace')]
body = [l for l in saved if pat.match(l)]
if not body:
    heads = [i for i, l in enumerate(lines) if l.startswith('CPU profiling warning counts:')] + [len(lines)]
    best = max(range(len(heads) - 1), key=lambda k: heads[k + 1] - heads[k])
    body = lines[heads[best] + 1:heads[best + 1]]
allc = 0
for l in body:
    m = pat.match(l)
    if not m: continue
    a = int(m.group(1), 16); ins, cyc, im, dh = map(int, m.group(3, 4, 5, 6))
    if base <= a < base + 0x100000:
        k = bisect.bisect_right(addrs, a - base) - 1
        name = syms[k][1] if k >= 0 else '?'
    elif a >= 0xe00000 and a < 0xf00000: name = '[TOS ROM]'
    else: name = '[other %06x]' % (a >> 16)
    t = tot.setdefault(name, [0, 0, 0, 0]); t[0] += ins; t[1] += cyc; t[2] += im; t[3] += dh
    allc += cyc
print('total cycles %d (%.1f ms/frame over 600 frames at 32.085 MHz)' % (allc, allc / 32.084988e6 * 1000 / 600))
print('%-30s %7s %9s %8s %8s %9s %8s' % ('function', 'cyc%', 'ms/frame', 'Minstr', 'cyc/ins', 'i-miss%', 'Mdhits'))
for name, (ins, cyc, im, dh) in sorted(tot.items(), key=lambda x: -x[1][1])[:N]:
    print('%-30s %6.2f%% %9.2f %8.2f %8.2f %8.1f%% %8.2f' % (name, 100.0 * cyc / allc, cyc / 32.084988e6 * 1000 / 600, ins / 1e6, cyc / max(ins, 1), 100.0 * im / max(ins, 1), dh / 1e6))
