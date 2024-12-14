#!/bin/bash

set -e
set -o pipefail

CFLAGS="-O1 -g -gdwarf-2 -fstandalone-debug -fno-eliminate-unused-debug-types -gline-tables-only"

./build/bin/mattc --verbose --emit-ir $1 2>&1 | tee log.log
clang-18 -O2 -c -o ${1%.*}.o ${1%.*}.ll
clang-18 -O2 -S -emit-llvm -o ${1%.*}.opt.ll ${1%.*}.ll
objdump -S ${1%.*}.o >${1%.*}.s
clang-18 -o ${1%.*} ${1%.*}.o -lm
