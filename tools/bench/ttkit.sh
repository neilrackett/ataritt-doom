#!/bin/bash
# ttkit.sh [DIR]: the kit for timing TTDOOM on a real TT (default
# tmp/ttkit). Copy DIR's files to one folder on the TT and run BENCH.TOS:
# it plays the timedemos in BENCH.TXT unattended (about 12 minutes on a
# TT) and writes RESULTS.TXT, and each run's output to RUNn.LOG.
#
# The programs are benchmark builds of this working tree (mkbuild.sh:
# each demo ends at tic 600 and prints the game state's hash, BENCH
# STATE): gcc 4.6.4 (GCC4.TTP), gcc 13 + LTO (LTO.TTP) and + PGO
# (PGO.TTP, pgo.sh), as $BENCH/bin/tt_gcc4, tt_lto and tt_pgo; NOBUILD=1
# takes those as they are. The kit is then run once under Hatari (TT, 2
# MB + 4 MB TT-RAM, TOS 3.06, VGA), and each BENCH.TXT line notes the
# realtics it gave there, which RESULTS.TXT compares the real ones with.
set -eo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
K=${1:-$REPO/tmp/ttkit}
TOS=${TOS:-$REPO/tmp/tos/tos306uk.img}
if [ -z "${HATARI:-}" ]; then
  HATARI=$(command -v hatari || echo /Applications/Hatari.app/Contents/MacOS/hatari)
fi
cd "$REPO"
for f in dist/DOOM1.WAD "$TOS"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

if [ "${NOBUILD:-}" != 1 ]; then
  "$HERE/mkbuild.sh" tt_gcc4 & P1=$!
  TOOLCHAIN=mintelf LTO=1 "$HERE/mkbuild.sh" tt_lto & P2=$!
  "$HERE/pgo.sh" tt_pgo & P3=$!
  wait $P1 && wait $P2 && wait $P3
fi
rm -rf "$K"; mkdir -p "$K"
cp "$BENCH/bin/tt_gcc4.TOS" "$K/GCC4.TTP"
cp "$BENCH/bin/tt_lto.TOS" "$K/LTO.TTP"
cp "$BENCH/bin/tt_pgo.TOS" "$K/PGO.TTP"
cp dist/DOOM1.WAD "$K/"
STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd m68k-atari-mint-gcc -O2 -Wall -s \
  -o "${K#$REPO/}/BENCH.TOS" tools/bench/ttbench.c
# BENCH.TOS loads and allocates in ST-RAM (program flags: fast load only),
# leaving TT-RAM to the program it times, as when that is started from the
# desktop. In TT-RAM BENCH took 350K there: on the TT the zone shrank to
# 1.83 MB and demo3 re-read 246 purged lumps (BENCH READS).
python3 -I - "$K/BENCH.TOS" <<'EOF'
import sys
f = open(sys.argv[1], "r+b")
f.seek(22)
f.write((1).to_bytes(4, "big"))
EOF

crlf() { sed 's/$/\r/' > "$1"; }
printf 'screenblocks 10\ndetaillevel 1\n' | crlf "$K/LOW.CFG"
printf 'screenblocks 10\ndetaillevel 0\n' | crlf "$K/HIGH.CFG"
# Most telling first: on a TT a run takes about 2 minutes (low) or 3
# (high), half that under Hatari. demo3 is the demo PGO didn't train on.
# The TT rounds of 2026-11-12 (AGENTS.md, "On a real TT") settled load
# alignment (+2), -cacr 0111, the builds, the lump re-reads and demo3's
# game logic. This one times the C2P (a run less its -noblit run) and
# demo1, which "Where the time goes" uses.
HEAD='# BENCH.TOS runs each line in turn: [+N] PROGRAM PARAMETERS [# NOTE]
# Each program loads on a 16-byte boundary, or N bytes past one with +N.
# Each demo ends at tic 600; the BENCH STATE hash there is the same on
# any machine (demo3: c3d20ea1).'
RUNS='PGO.TTP -config LOW.CFG -timedemo demo3
PGO.TTP -config LOW.CFG -timedemo demo3 -noblit
PGO.TTP -config HIGH.CFG -timedemo demo3
PGO.TTP -config HIGH.CFG -timedemo demo3 -noblit
PGO.TTP -config LOW.CFG -timedemo demo1
PGO.TTP -config LOW.CFG -timedemo demo1 -nodraw -nosound'
printf '%s\n%s\n' "$HEAD" "$RUNS" | crlf "$K/BENCH.TXT"

# The kit under Hatari's TT, for the notes
R=$BENCH/runs/ttkit
rm -rf "$R"; mkdir -p "$R/hd"; cp "$K"/* "$R/hd/"
: > "$R/hatari.cfg"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$HATARI" --configfile "$R/hatari.cfg" \
  --tos "$TOS" --machine tt --mmu off --monitor vga --memsize 2 --ttram 4 \
  --addr24 off --cpu-exact on --data-cache on --fast-forward on \
  --confirm-quit false --fast-boot on --sound 44100 --statusbar off \
  --alert-level fatal --conout 2 --cmd-fifo "$R/fifo" --harddrive "$R/hd" \
  --auto 'C:\BENCH.TOS' > "$R/log" 2>&1 &
HP=$!
until grep -aq "BENCH: done" "$R/log"; do
  kill -0 $HP 2>/dev/null || { echo "Hatari stopped early: $R/log" >&2; exit 1; }
  sleep 1
done
echo "hatari-debug quit" > "$R/fifo"; sleep 2; kill -9 $HP 2>/dev/null || true
tr -d '\r' < "$R/hd/RESULTS.TXT" | tee "$R/results.txt"

# BENCH.TXT again with each run's realtics under Hatari
i=0
{
  printf '%s\n' "$HEAD"
  while IFS= read -r run; do
    i=$((i + 1))
    rt=$(awk -v i=$i '$1 == i && /realtics/ { sub(/.*: /, ""); print $1; exit }' "$R/results.txt")
    [ -n "$rt" ] || { echo "no Hatari result for run $i: $run" >&2; exit 1; }
    printf '%s # Hatari %s\n' "$run" "$rt"
  done <<< "$RUNS"
} | crlf "$K/BENCH.TXT"
rm -f "$K"/RESULTS.TXT "$K"/RUN*.LOG
ls -l "$K" | awk 'NR > 1 { print $5, $9 }'
