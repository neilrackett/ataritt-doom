#!/bin/bash
# rcheck.sh LABEL: does LABEL's new renderer code draw what its old code
# draws? LABEL is a build made with RCHECK=1 (mkbuild.sh), which keeps the
# code a change replaces (under R_CHECK in the source) and, with -rcheck,
# draws every 3D view with both and compares them (benchpatch.py).
#
# Demos 1 to 3 in low and high detail at view sizes 10, 9 and 5, 600 tics
# each, and the fuzz player (fuzz.sh) on E1M1 to E1M9, 1500 tics a map in
# alternating detail. Every run must report "0 differ". JOBS Hatari at a
# time (default 4: no more than 6 in all).
set -u
LABEL=$1
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
export BENCH=${BENCH:-$REPO/tmp/bench}
OUT=$BENCH/rcheck_$LABEL.txt
: > "$OUT"
gate() { while [ "$(jobs -rp | wc -l)" -ge "${JOBS:-4}" ]; do sleep 1; done; }
for demo in demo1 demo2 demo3; do
  for mode in low high; do
    for size in 10 9 5; do
      gate
      DEMO=$demo SIZE=$size TAG=rc$size EXTRAARGS=-rcheck "$HERE/run.sh" "$LABEL" $mode >> "$OUT" 2>&1 &
    done
  done
done
for map in 1 2 3 4 5 6 7 8 9; do
  gate
  mode=low; [ $((map % 2)) = 0 ] && mode=high
  RAWARGS="-warp 1 $map -skill 4 -nosound -benchfuzz ${SEED:-7} -benchtics ${TICS:-1500} -rcheck" \
    TAG=rcfz$map "$HERE/run.sh" "$LABEL" $mode >> "$OUT" 2>&1 &
done
wait
n=$(grep -c "RCHECK frames" "$OUT")
bad=$(grep -c "RCHECK first" "$OUT")
frames=$(grep -a "RCHECK frames" "$BENCH"/runs/tt_${LABEL}_*_rc*/log | sed 's/.*RCHECK frames \([0-9]*\).*/\1/' | awk '{s += $1} END {print s + 0}')
if [ "$n" = 27 ] && [ "$bad" = 0 ]; then
  echo "$LABEL: 27 runs, $frames frames drawn both ways, no differences"
else
  echo "$LABEL: $n of 27 runs finished, $bad with differences: $OUT"
  grep "RCHECK first" "$OUT"
  exit 1
fi
