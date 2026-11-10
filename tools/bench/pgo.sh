#!/bin/bash
# pgo.sh LABEL [DEMO:MODE ...]
#
# TTDOOM from this working tree, built with gcc 13 and profile-guided
# optimisation, as $BENCH/bin/LABEL.TOS (symbols in nm_LABEL.txt):
#  1. mkbuild.sh LABEL_gen with PGO=gen: a TTDOOM that writes its profile
#     to PGO.DAT when a timedemo ends (src/pgo.c);
#  2. runs it on the training timedemos, DEMO:MODE as run.sh takes them
#     (default demo1:low demo1:high demo2:low demo2:high, leaving demo3
#     unseen to check the profile isn't just fitted to its demos);
#  3. merges their profiles into .gcda files beside the objects
#     (gcov-tool merge-stream, in the toolkit image);
#  4. rebuilds in the same tree with PGO=use.
# LTO=1 by default (LTO=0 for PGO alone). Needs gcc 13 in the toolkit image
# (STCMD_IMAGE_TAG picks another image).
set -eo pipefail
LABEL=$1; shift
RUNS=${*:-demo1:low demo1:high demo2:low demo2:high}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
export TOOLCHAIN=mintelf LTO=${LTO:-1} STCMD_NO_TTY=1 STCMD_QUIET=1
# The Makefile's folders for this build (VARIANT)
V=mintelf
[ "$LTO" = 1 ] && V=$V-lto
[ "${FASTCALL:-}" = 1 ] && V=$V-fastcall
V=$V-pgo

PGO=gen "$HERE/mkbuild.sh" "${LABEL}_gen"
T=$BENCH/tree_${LABEL}_gen

# Training runs, in parallel (at most six): each writes its drive's PGO.DAT
for r in $RUNS; do
  DEMO=${r%%:*} TAG=pgo "$HERE/run.sh" "${LABEL}_gen" "${r#*:}" &
done
wait
n=0
for r in $RUNS; do
  d=$BENCH/runs/tt_${LABEL}_gen_${r#*:}_${r%%:*}_pgo/hd/PGO.DAT
  [ -s "$d" ] || { echo "no profile from $r ($d)" >&2; exit 1; }
  cp "$d" "$T/pgo$n.dat"; n=$((n + 1))
done

# The .gcda files go where the stream names them: the objects' folder, at
# the path the tree had in the container when it was built
cd "$T"
for f in pgo*.dat; do
  stcmd m68k-atari-mintelf-gcov-tool merge-stream "$f"
done
# The stream has no whole-program summary: write it in (gcdasum.py)
python3 -I "$HERE/gcdasum.py" $n build/$V/*.gcda

# Rebuild with them: the objects and program go, the profiles stay
rm -f build/$V/*.o "dist/$V/TTDOOM.TOS"
stcmd make ttdoom TOOLCHAIN=mintelf LTO=$LTO FASTCALL=${FASTCALL:-0} PGO=use > "$BENCH/build_$LABEL.log" 2>&1 \
  || { tail -30 "$BENCH/build_$LABEL.log"; exit 1; }
cp "dist/$V/TTDOOM.TOS" "$BENCH/bin/$LABEL.TOS"
stcmd m68k-atari-mintelf-nm -n "dist/$V/TTDOOM.TOS" 2>/dev/null | grep -v ST_WORKING > "$BENCH/nm_$LABEL.txt"
echo "built $BENCH/bin/$LABEL.TOS"
