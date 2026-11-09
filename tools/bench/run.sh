#!/bin/bash
# run.sh LABEL MODE [prof]
#
# Runs $BENCH/bin/LABEL.TOS (mkbuild.sh) on a 600-tic timedemo under Hatari,
# headless and fast-forwarded (emulated time ignores host load, so runs can
# go in parallel: at most six), and prints the realtics, the time a frame
# and the BENCH STATE game-state hash.
#
# MODE: low | high (detail, screenblocks 10), with an optional suffix:
#   nodraw (-nodraw -nosound: the game logic a tic), nosfx, nosound.
# prof: also a per-function profile of the demo, from its first tic, in the
# run's log; profagg.py sums it per function.
#
# Environment:
#   MACHINE  tt (default): the target TT, TOS 3.06, 2 MB ST-RAM + 4 MB
#            TT-RAM, VGA, cycle-exact with the data cache
#            falcon: EmuTOS 512k ($ETOS512), VGA (or MON=rgb), 14 MB
#            ste: TOS 1.62, 4 MB, for STDOOM (mkbuild.sh LABEL SRC stdoom)
#   DEMO     demo1 (default), demo2, demo3
#   EXTRAARGS more arguments (-benchshot N, -mixtest, -c2ptest, -cacr X)
#   TAG      a suffix for the run's folder, to tell parallel runs apart
#   HATARI   the emulator (default: hatari on the PATH, else the Mac app)
#   BENCH    the work folder (default tmp/bench)
#
# A run's files (log, the hard disk folder with any dumps) are in
# $BENCH/runs/MACHINE_LABEL_MODE[_DEMO][_TAG][_prof].
set -u
LABEL=$1; MODE=$2; PROF=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
MACHINE=${MACHINE:-tt}
if [ -z "${HATARI:-}" ]; then
  HATARI=$(command -v hatari || echo /Applications/Hatari.app/Contents/MacOS/hatari)
fi
R=$BENCH/runs/${MACHINE}_${LABEL}_$MODE${DEMO:+_$DEMO}${TAG:+_$TAG}${PROF:+_prof}
[ -f "$BENCH/bin/$LABEL.TOS" ] || { echo "no $BENCH/bin/$LABEL.TOS" >&2; exit 1; }
rm -rf "$R"; mkdir -p "$R/hd"
PROG=TTDOOM.TOS; [ "$MACHINE" = ste ] && PROG=STDOOM.TOS
cp "$BENCH/bin/$LABEL.TOS" "$R/hd/$PROG"
cp "$REPO/dist/DOOM1.WAD" "$R/hd/"
case $MODE in
  low*)  DETAIL=1 ;;
  high*) DETAIL=0 ;;
esac
ARGS="-timedemo ${DEMO:-demo1}"
case $MODE in
  *nodraw)  ARGS="$ARGS -nodraw -nosound" ;;
  *nosfx)   ARGS="$ARGS -nosfx" ;;
  *nosound) ARGS="$ARGS -nosound" ;;
esac
[ -n "${EXTRAARGS:-}" ] && ARGS="$ARGS $EXTRAARGS"
# STDOOM keeps its own default view size
if [ "$MACHINE" = ste ]; then
  printf 'detaillevel %s\n' "$DETAIL" > "$R/hd/doomrc.cfg"
else
  printf 'screenblocks 10\ndetaillevel %s\n' "$DETAIL" > "$R/hd/doomrc.cfg"
fi
printf '%s\n' "$ARGS" > "$R/hd/ARGS.TXT"
: > "$R/hatari.cfg"
case $MACHINE in
  tt)     MACH="--tos $REPO/tmp/tos/tos306uk.img --machine tt --mmu off --monitor vga
            --memsize 2 --ttram 4 --addr24 off --cpu-exact on --data-cache on" ;;
  falcon) MACH="--tos ${ETOS512:-$REPO/tmp/tos/etos512uk.img} --machine falcon --mmu off
            --fpu none --monitor ${MON:-vga} --memsize 14 --dsp none" ;;
  ste)    MACH="--tos $REPO/tmp/tos/tos162uk.img --machine ste --memsize 4" ;;
esac
FIFO=$R/fifo
LOG=$R/log
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$HATARI" --configfile "$R/hatari.cfg" \
  $MACH --fast-forward on --confirm-quit false --fast-boot on --sound 44100 \
  --statusbar off --alert-level fatal --conout 2 --cmd-fifo "$FIFO" \
  --harddrive "$R/hd" --auto "C:\\$PROG" > "$LOG" 2>&1 &
HPID=$!
for i in $(seq 1 50); do [ -p "$FIFO" ] && break; sleep 0.2; done
waitfor() {
  for i in $(seq 1 ${2:-2400}); do
    grep -q "$1" "$LOG" && return 0
    kill -0 $HPID 2>/dev/null || return 1
    sleep 0.5
  done
  return 1
}
if [ -n "$PROF" ]; then
  # Symbols once the program runs; profiling from the first demo tic, and
  # at I_Error (the timedemo's result) the profile is written to the log
  # and Hatari quits
  printf 'profile save %s/profile.txt\nquit\n' "$R" > "$R/prof.ini"
  printf 'profile on\n' > "$R/profon.ini"
  waitfor "BENCH READY" || echo "no BENCH READY" >&2
  echo "hatari-debug symbols prg" > "$FIFO"
  echo "hatari-debug b pc = _G_ReadDemoTiccmd :once :quiet :file $R/profon.ini" > "$FIFO"
  echo "hatari-debug b pc = _I_Error :quiet :file $R/prof.ini" > "$FIFO"
  # prof.ini quits Hatari once the profile is out
  for i in $(seq 1 4800); do kill -0 $HPID 2>/dev/null || break; sleep 0.5; done
else
  waitfor "timed .* gametics" || echo "$LABEL $MODE: no result" >&2
  echo "hatari-debug quit" > "$FIFO" 2>/dev/null
fi
for i in $(seq 1 20); do kill -0 $HPID 2>/dev/null || break; sleep 0.5; done
kill -9 $HPID 2>/dev/null
N=$(grep -o "timed [0-9]* gametics in [0-9]* realtics" "$LOG" | head -1 | awk '{print $5}')
ST=$(grep -o "BENCH STATE.*" "$LOG" | head -1 | sed 's/BENCH STATE //')
if [ -n "$N" ]; then
  awk -v n="$N" -v l="$MACHINE-$LABEL" -v m="$MODE${DEMO:+-$DEMO}${TAG:+-$TAG}" -v st="$ST" \
    'BEGIN { printf "%-16s %-16s %6d realtics %7.1f ms/frame  %s\n", l, m, n, n*1000/35/600, st }'
elif [ -n "$PROF" ]; then
  echo "$MACHINE-$LABEL $MODE: profile in $LOG"
else
  echo "$MACHINE-$LABEL $MODE: FAILED (see $LOG)"
fi
