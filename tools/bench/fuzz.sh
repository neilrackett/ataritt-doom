#!/bin/bash
# fuzz.sh LABEL [BASE]: is LABEL's game logic BASE's (default: base)?
#
# Plays $BENCH/bin/LABEL.TOS (mkbuild.sh) on generated input (-benchfuzz:
# a private generator's ticcmds, god mode, skill 4) for 3000 tics on each
# of E1M1 to E1M9, plain, with -respawn and with -fast, nothing drawn. A
# run prints a hash of the game state in depth every 35 tics (benchpatch.py);
# the streams go to $BENCH/fuzz/LABEL/ and are compared with BASE's. Far
# more of the game than the demos' 600 tics reach: every map, monsters
# woken and fighting, doors, lifts, crushers, respawns.
#
# Environment: MAPS (default "1 2 3 4 5 6 7 8 9"), VARIANTS (default
# "plain respawn fast"), TICS (3000), SEED (7), JOBS (Hatari at a time,
# default 4: no more than 6 in all), MACHINE (run.sh's; ste for STDOOM).
set -u
LABEL=$1; BASE=${2:-base}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
export BENCH=${BENCH:-$REPO/tmp/bench}
MACHINE=${MACHINE:-tt}
OUT=$BENCH/fuzz/$LABEL
mkdir -p "$OUT"
for map in ${MAPS:-1 2 3 4 5 6 7 8 9}; do
  for v in ${VARIANTS:-plain respawn fast}; do
    while [ "$(jobs -rp | wc -l)" -ge "${JOBS:-4}" ]; do sleep 1; done
    (
      opt=; [ "$v" = plain ] || opt=" -$v"
      RAWARGS="-warp 1 $map -skill 4$opt -nodraw -nosound -benchfuzz ${SEED:-7} -benchtics ${TICS:-3000}" \
        TAG=fz_m${map}_$v "$HERE/run.sh" "$LABEL" low > /dev/null 2>&1
      grep -a "^FUZZ \|fuzzed [0-9]* tics" "$BENCH/runs/${MACHINE}_${LABEL}_low_fz_m${map}_$v/log" \
        | tr -d '\r' > "$OUT/m${map}_$v.txt"
    ) &
  done
done
wait

bad=0; n=0
for f in "$OUT"/m*.txt; do
  n=$((n + 1))
  if ! grep -q "fuzzed" "$f"; then
    echo "$LABEL $(basename "$f" .txt): did not finish"; bad=1
  elif [ "$LABEL" != "$BASE" ]; then
    b=$BENCH/fuzz/$BASE/$(basename "$f")
    if [ ! -f "$b" ]; then
      echo "$LABEL $(basename "$f" .txt): no $BASE stream"; bad=1
    elif ! cmp -s "$f" "$b"; then
      echo "$LABEL $(basename "$f" .txt): DIFFERS from $BASE at $(diff "$b" "$f" | sed -n 2p)"; bad=1
    fi
  fi
done
h=$(cat "$OUT"/m*.txt | cksum | awk '{print $1}')
if [ $bad = 0 ]; then
  if [ "$LABEL" = "$BASE" ]; then
    echo "$LABEL: $n streams, $(cat "$OUT"/m*.txt | grep -c '^FUZZ') hashes (cksum $h)"
  else
    echo "$LABEL: $n streams, all equal to $BASE's (cksum $h)"
  fi
fi
exit $bad
