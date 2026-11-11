#!/bin/bash
# ttkit.sh [DIR]: the kit for timing TTDOOM on a real TT (default
# tmp/ttkit). Copy DIR's files to one folder on the TT and run BENCH.TOS:
# it plays the timedemos in BENCH.TXT unattended (about an hour) and
# writes RESULTS.TXT, and each run's output to RUNn.LOG.
#
# The programs are the dist builds as they are: gcc 4.6.4 (GCC4.TTP),
# gcc 13 + LTO (LTO.TTP) and + PGO (PGO.TTP, from pgo.sh --dist). Each
# BENCH.TXT line notes the realtics the same run gave under Hatari (TT,
# 2 MB + 4 MB TT-RAM, TOS 3.06, VGA; this kit's BENCH.TOS, whole demos),
# which RESULTS.TXT compares the real ones with. Measure them again when
# the builds change.
set -eo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
K=${1:-$REPO/tmp/ttkit}
cd "$REPO"
for f in dist/TTDOOM.TOS dist/mintelf-lto/TTDOOM.TOS dist/mintelf-lto-pgo/TTDOOM.TOS dist/DOOM1.WAD; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done
rm -rf "$K"; mkdir -p "$K"
cp dist/TTDOOM.TOS "$K/GCC4.TTP"
cp dist/mintelf-lto/TTDOOM.TOS "$K/LTO.TTP"
cp dist/mintelf-lto-pgo/TTDOOM.TOS "$K/PGO.TTP"
cp dist/DOOM1.WAD "$K/"
STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd m68k-atari-mint-gcc -O2 -Wall -s \
  -o "${K#$REPO/}/BENCH.TOS" tools/bench/ttbench.c

crlf() { sed 's/$/\r/' > "$1"; }
printf 'screenblocks 10\ndetaillevel 1\n' | crlf "$K/LOW.CFG"
printf 'screenblocks 10\ndetaillevel 0\n' | crlf "$K/HIGH.CFG"
crlf "$K/BENCH.TXT" <<'EOF'
# BENCH.TOS runs each line in turn: [+N] PROGRAM PARAMETERS [# NOTE]
# Each program loads on a 16-byte boundary, or N bytes past one with +N.
# Most telling first; demo1 runs take about 7 min (low) or 11 (high),
# demo3 about 3.
GCC4.TTP -config LOW.CFG -timedemo demo1 # Hatari 15329
PGO.TTP -config LOW.CFG -timedemo demo1 # Hatari 14581
PGO.TTP -config LOW.CFG -timedemo demo3 # Hatari 6233
+2 PGO.TTP -config LOW.CFG -timedemo demo3 # Hatari 6312
GCC4.TTP -config LOW.CFG -timedemo demo3 # Hatari 6572
+2 GCC4.TTP -config LOW.CFG -timedemo demo3 # Hatari 6802
PGO.TTP -config LOW.CFG -timedemo demo3 -cacr 0111 # Hatari 5664
GCC4.TTP -config HIGH.CFG -timedemo demo1 # Hatari 23296
PGO.TTP -config HIGH.CFG -timedemo demo1 # Hatari 21963
LTO.TTP -config LOW.CFG -timedemo demo1 # Hatari 15007
PGO.TTP -config LOW.CFG -timedemo demo3 -cacr 3111 # Hatari 6239
PGO.TTP -config LOW.CFG -timedemo demo3 -nosound # Hatari 6116
PGO.TTP -config LOW.CFG -timedemo demo3 # Hatari 6233
EOF
ls -l "$K" | awk 'NR > 1 { print $5, $9 }'
