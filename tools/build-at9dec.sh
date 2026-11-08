#!/bin/sh
# Builds tools/at9dec.c (for tools/fsb5.py) into build/at9dec with the host's
# C compiler, against the LibAtrac9 a bbhost checkout bundles.
#   tools/build-at9dec.sh [BBHOST_SRC]   (or set $BBHOST_SRC)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
bbhost=${1:-${BBHOST_SRC:?usage: tools/build-at9dec.sh BBHOST_SRC (or set BBHOST_SRC)}}
src=$bbhost/third_party/libatrac9/C/src
mkdir -p "$here/build"
${CC:-cc} -O2 -w -I"$src" -o "$here/build/at9dec" "$here/tools/at9dec.c" "$src"/*.c -lm
echo "built $here/build/at9dec"
