#!/bin/bash
# runkit.sh NAME: runs BENCH.TXT lines (stdin) through BENCH.TOS under
# Hatari's TT and prints RESULTS.TXT. What run.sh can't do: BENCH.TOS
# loads each program on a 16-byte boundary or N bytes past one (+N), so
# builds are compared at the same load alignment, and one that only gains
# at one alignment is found out.
#
# A line: [+N] LABEL.TTP PARAMETERS, LABEL a mkbuild.sh build
# ($BENCH/bin/LABEL.TOS, at most 8 characters). LOW.CFG and HIGH.CFG
# (size 10) are there for -config. For example:
#   printf '%s\n' 'cur.TTP -config LOW.CFG -timedemo demo3' \
#     '+2 cur.TTP -config LOW.CFG -timedemo demo3' \
#     'cur.TTP -config LOW.CFG -timedemo demo3 -cacr 0111' | runkit.sh align
# The run's files are in $BENCH/runs/kit_NAME.
set -eo pipefail
NAME=$1
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
TOS=${TOS:-$REPO/tmp/tos/tos306uk.img}
if [ -z "${HATARI:-}" ]; then
  HATARI=$(command -v hatari || echo /Applications/Hatari.app/Contents/MacOS/hatari)
fi
R=$BENCH/runs/kit_$NAME
rm -rf "$R"; mkdir -p "$R/hd"
cd "$REPO"

# BENCH.TOS, in ST-RAM (as ttkit.sh builds it)
B=$BENCH/bin/BENCH.TOS
if [ ! -f "$B" ] || [ tools/bench/ttbench.c -nt "$B" ]; then
  STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd m68k-atari-mint-gcc -O2 -Wall -s \
    -o "${B#$REPO/}" tools/bench/ttbench.c
  python3 -I - "$B" <<'EOF'
import sys
f = open(sys.argv[1], "r+b")
f.seek(22)
f.write((1).to_bytes(4, "big"))
EOF
fi
cp "$B" "$R/hd/BENCH.TOS"
cp dist/DOOM1.WAD "$R/hd/"
crlf() { sed 's/$/\r/' > "$1"; }
printf 'screenblocks 10\ndetaillevel 1\n' | crlf "$R/hd/LOW.CFG"
printf 'screenblocks 10\ndetaillevel 0\n' | crlf "$R/hd/HIGH.CFG"
LINES=$(cat)
for prog in $(printf '%s\n' "$LINES" | grep -o '[A-Za-z0-9_]*\.TTP' | sort -u); do
  label=${prog%.TTP}
  [ -f "$BENCH/bin/$label.TOS" ] || { echo "no $BENCH/bin/$label.TOS" >&2; exit 1; }
  cp "$BENCH/bin/$label.TOS" "$R/hd/$(printf '%s' "$prog" | tr a-z A-Z)"
done
printf '%s\n' "$LINES" | crlf "$R/hd/BENCH.TXT"

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
tr -d '\r' < "$R/hd/RESULTS.TXT" | grep -v "68030 caches\|BENCH READS\|^BENCH:" | tee "$R/results.txt"
