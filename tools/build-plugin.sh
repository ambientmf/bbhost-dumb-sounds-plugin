#!/bin/sh
# Builds plugin/hitmarker.c into a Windows DLL with clang + mingw-w64.
#   tools/build-plugin.sh [BBHOST_SRC] [OUT_DIR]
# BBHOST_SRC: a bbhost checkout (for include/bbhost_plugin.h); or set $BBHOST_SRC
# OUT_DIR:    where the DLL goes, default build/
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
bbhost=${1:-${BBHOST_SRC:?usage: tools/build-plugin.sh BBHOST_SRC [OUT_DIR] (or set BBHOST_SRC)}}
outdir=${2:-$here/build}
mkdir -p "$outdir"
prlimit --as=4000000000 clang --target=x86_64-w64-mingw32 -O2 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -shared \
    -I"$bbhost/include" -I"$here/third_party" -o "$outdir/hitmarker.dll" "$here/plugin/hitmarker.c" \
    -static -lole32
echo "built $outdir/hitmarker.dll"
