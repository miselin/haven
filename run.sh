#!/bin/bash

set -e
set -o pipefail

gcc -E - <$1 >$1.preproc
./build/src/mattc $1.preproc $1.ir 2>&1 | tee log.log

opt-18 -S --O3 $1.ir >$1.ll
llc-18 -O2 -filetype=obj $1.ll -o $1.o
llc-18 -O2 -filetype=asm $1.ll -o $1.s
clang-18 -o $1.bin $1.o -lm
