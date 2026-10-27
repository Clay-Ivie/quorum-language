#!/bin/sh
# Builds a native executable from LLVM IR produced by `quorum -llvm`.
#   usage: build_native.sh <file.ll> [output-name]
# Set QUORUM_USE_GC=1 to link the Boehm garbage collector (needs libgc).
set -e
if [ -z "$1" ]; then echo "usage: $0 <file.ll> [output-name]"; exit 1; fi
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${2:-$(basename "$1" .ll)}"
CC="${CC:-clang}"
if [ "$QUORUM_USE_GC" = "1" ]; then
  "$CC" -O2 -Wno-override-module -DQUORUM_USE_GC "$1" "$HERE/quorum_runtime.c" -lgc -o "$OUT"
else
  "$CC" -O2 -Wno-override-module "$1" "$HERE/quorum_runtime.c" -o "$OUT"
fi
echo "Built $OUT"
