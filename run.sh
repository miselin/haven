#!/bin/bash

set -e
set -o pipefail

CFLAGS="-O2 -g -gdwarf-2 -fstandalone-debug -fno-eliminate-unused-debug-types -gline-tables-only"

gcc -E - <$1 >$1.preproc
./build/bin/mattc $1.preproc $1.ll 2>&1 | tee log.log

clang-18 -c ${CFLAGS} -o $1.o -x ir $1.ll
clang-18 -S ${CFLAGS} -emit-llvm -o $1.opt.ll $1.ll
objdump -S $1.o >$1.s

clang-18 ${CFLAGS} -o $1.bin $1.o -lm
