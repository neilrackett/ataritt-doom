#!/usr/bin/env python3
# pagecheck.py DIR: decodes each PN.RAW (TT Low page, 320x480, 8 interleaved
# planes, each picture line twice) and compares it with SN.RAW (the chunky
# frame it was converted from).
import sys, os, glob
d = sys.argv[1]
for fp in sorted(glob.glob(os.path.join(d, 'P*.RAW')), key=lambda f: int(os.path.basename(f)[1:-4])):
    t = os.path.basename(fp)[1:-4]
    page = open(fp, 'rb').read()
    chunky = open(os.path.join(d, 'S%s.RAW' % t), 'rb').read()
    bad = 0; badrows = set()
    for l in range(240):
        for half in (0, 1):
            line = page[(2 * l + half) * 320:(2 * l + half + 1) * 320]
            for g in range(20):
                w = [(line[g*16 + 2*k] << 8) | line[g*16 + 2*k + 1] for k in range(8)]
                for i in range(16):
                    v = 0
                    for k in range(8):
                        v |= ((w[k] >> (15 - i)) & 1) << k
                    if v != chunky[l * 320 + g * 16 + i]:
                        bad += 1; badrows.add(l)
    print('tic %-4s %6d pixels differ, rows %s' % (t, bad, sorted(badrows)[:12]))
