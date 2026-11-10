#!/bin/bash
# mkbuild.sh LABEL [SRC | @REF] [TARGET]
#
# Copies the tree to $BENCH/tree_LABEL, patches it for benchmarking
# (benchpatch.py), builds TARGET (ttdoom, or stdoom) in it with stcmd and
# keeps the program as $BENCH/bin/LABEL.TOS, with its symbols (nm -n) in
# $BENCH/nm_LABEL.txt for profagg.py.
#
# SRC: a tree to copy (default: this working tree, changes and all).
# @REF: a commit instead, e.g. @HEAD for the last commit without the
# working tree's changes: exported with git archive, the submodules
# (src/atarist-sfp004, lib/xpad) copied from the working tree.
#
# BENCH is the work folder: tmp/bench by default (tmp/ is not in git).
# TOOLCHAIN=mintelf builds with gcc 13 (the Makefile's TOOLCHAIN) instead of
# gcc 4.6.4; with it, LTO=1, FASTCALL=1 and PGO=gen|use go to the Makefile
# too (PGO: pgo.sh).
set -eo pipefail
LABEL=$1
SRC=${2:-}
TARGET=${3:-ttdoom}
TOOLCHAIN=${TOOLCHAIN:-mint}
MAKEVARS="TOOLCHAIN=$TOOLCHAIN${LTO:+ LTO=$LTO}${FASTCALL:+ FASTCALL=$FASTCALL}${PGO:+ PGO=$PGO}"
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BENCH=${BENCH:-$REPO/tmp/bench}
T=$BENCH/tree_$LABEL
mkdir -p "$BENCH/bin"
rm -rf "$T"; mkdir -p "$T"
case $SRC in
  @*)
    git -C "$REPO" archive "${SRC#@}" src Makefile | tar -x -C "$T"
    rsync -a "$REPO/src/atarist-sfp004/" "$T/src/atarist-sfp004/"
    mkdir -p "$T/lib"; rsync -a "$REPO/lib/xpad" "$T/lib/" ;;
  *)
    rsync -a --exclude .git --exclude build --exclude dist --exclude sidecart \
      --exclude tmp --exclude docs "${SRC:-$REPO}/" "$T/" ;;
esac
python3 -I "$HERE/benchpatch.py" "$T"
cd "$T"
export STCMD_NO_TTY=1
OUT=TTDOOM.TOS; [ "$TARGET" = stdoom ] && OUT=STDOOM.TOS
# The Makefile's dist folder for this build (VARIANT)
DIST=dist
if [ "$TOOLCHAIN" != mint ]; then
  DIST=dist/$TOOLCHAIN
  [ "${LTO:-}" = 1 ] && DIST=$DIST-lto
  [ "${FASTCALL:-}" = 1 ] && DIST=$DIST-fastcall
  [ -n "${PGO:-}" ] && DIST=$DIST-pgo
fi
stcmd make "$TARGET" $MAKEVARS > "$BENCH/build_$LABEL.log" 2>&1 || { tail -30 "$BENCH/build_$LABEL.log"; exit 1; }
cp "$DIST/$OUT" "$BENCH/bin/$LABEL.TOS"
stcmd m68k-atari-$TOOLCHAIN-nm -n "$DIST/$OUT" 2>/dev/null | grep -v ST_WORKING > "$BENCH/nm_$LABEL.txt"
echo "built $BENCH/bin/$LABEL.TOS"
