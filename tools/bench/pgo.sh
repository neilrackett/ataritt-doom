#!/bin/bash
# pgo.sh LABEL [DEMO:MODE ...]
# pgo.sh --dist [DEMO:MODE ...]
#
# TTDOOM built with gcc 13 and profile-guided optimisation:
#  1. builds it with PGO=gen: a TTDOOM that takes its arguments from
#     ARGS.TXT and writes its profile to PGO.DAT when a timedemo ends
#     (src/pgo.c, through the hooks in src/pgo.h);
#  2. runs that on the training timedemos, DEMO:MODE as run.sh takes them
#     (default demo1:low demo1:high demo2:low demo2:high, leaving demo3
#     unseen to check the profile isn't just fitted to its demos);
#  3. merges their profiles into .gcda files beside the objects
#     (gcov-tool merge-stream, in the toolkit image) and writes in the
#     whole-program summary the stream leaves out (gcdasum.py);
#  4. rebuilds with PGO=use.
#
# LABEL: a benchmark build of this working tree (mkbuild.sh: patched, the
# demos ending at tic 600), as $BENCH/bin/LABEL.TOS.
# --dist: this tree as it is, the demos played in full, into
# dist/mintelf-lto-pgo/TTDOOM.TOS (its build folder keeps the profile).
# With LTO and without FASTCALL (the release's build) the profile is also
# copied to tools/pgo/profile, which is in git: the release workflow has
# no Hatari to record one, and builds with that
# (make ttdoom TOOLCHAIN=mintelf LTO=1 PGO=use PROFILE=tools/pgo/profile).
# Commit it with the source it was recorded from.
#
# LTO=1 by default (LTO=0 for PGO alone); FASTCALL=1 as the Makefile takes
# it. Needs gcc 13 in the toolkit image (STCMD_IMAGE_TAG picks another).
set -eo pipefail
LABEL=$1; shift
RUNS=${*:-demo1:low demo1:high demo2:low demo2:high}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
export TOOLCHAIN=mintelf LTO=${LTO:-1} FASTCALL=${FASTCALL:-0} STCMD_NO_TTY=1 STCMD_QUIET=1
MAKEVARS="TOOLCHAIN=mintelf LTO=$LTO FASTCALL=$FASTCALL"
# The Makefile's folders for this build (VARIANT)
V=mintelf
[ "$LTO" = 1 ] && V=$V-lto
[ "$FASTCALL" = 1 ] && V=$V-fastcall
V=$V-pgo
mkdir -p "$BENCH/bin"

# 1. The profiling build, as $BENCH/bin/$GEN.TOS for run.sh
if [ "$LABEL" = --dist ]; then
  T=$REPO
  GEN=dist_gen
  cd "$T"
  rm -rf "build/$V" "dist/$V"
  stcmd make ttdoom $MAKEVARS PGO=gen > "$BENCH/build_$GEN.log" 2>&1 \
    || { tail -30 "$BENCH/build_$GEN.log"; exit 1; }
  cp "dist/$V/TTDOOM.TOS" "$BENCH/bin/$GEN.TOS"
else
  GEN=${LABEL}_gen
  PGO=gen "$HERE/mkbuild.sh" "$GEN"
  T=$BENCH/tree_$GEN
fi

# 2. Training runs, in parallel (at most six): each writes its drive's PGO.DAT
for r in $RUNS; do
  DEMO=${r%%:*} TAG=pgo "$HERE/run.sh" "$GEN" "${r#*:}" > /dev/null &
done
wait

# 3. The .gcda files go where the stream names them: the objects' folder, at
# the path the tree had in the container when it was built
cd "$T"
n=0
for r in $RUNS; do
  d=$BENCH/runs/tt_${GEN}_${r#*:}_${r%%:*}_pgo/hd/PGO.DAT
  [ -s "$d" ] || { echo "no profile from $r ($d)" >&2; exit 1; }
  cp "$d" "build/$V/pgo$n.dat"
  stcmd m68k-atari-mintelf-gcov-tool merge-stream "build/$V/pgo$n.dat"
  n=$((n + 1))
done
python3 -I "$HERE/gcdasum.py" $n build/$V/*.gcda

# 4. Rebuild with them: the objects and program go, the profiles stay
rm -f build/$V/*.o "dist/$V/TTDOOM.TOS"
if [ "$LABEL" = --dist ]; then
  stcmd make ttdoom $MAKEVARS PGO=use > "$BENCH/build_dist.log" 2>&1 \
    || { tail -30 "$BENCH/build_dist.log"; exit 1; }
  echo "built $T/dist/$V/TTDOOM.TOS"
  if [ "$V" = mintelf-lto-pgo ]; then
    rm -rf tools/pgo/profile; mkdir -p tools/pgo/profile
    cp build/$V/*.gcda tools/pgo/profile/
    echo "profile in tools/pgo/profile ($(ls tools/pgo/profile | wc -l | tr -d ' ') files)"
  fi
else
  stcmd make ttdoom $MAKEVARS PGO=use > "$BENCH/build_$LABEL.log" 2>&1 \
    || { tail -30 "$BENCH/build_$LABEL.log"; exit 1; }
  cp "dist/$V/TTDOOM.TOS" "$BENCH/bin/$LABEL.TOS"
  stcmd m68k-atari-mintelf-nm -n "dist/$V/TTDOOM.TOS" 2>/dev/null | grep -v ST_WORKING > "$BENCH/nm_$LABEL.txt"
  echo "built $BENCH/bin/$LABEL.TOS"
fi
