#!/bin/bash

set -e
set -o pipefail

gcc -E -P - <$1 >$1.preproc
./build/src/mattc >$1.ir <$1.preproc

cat $1.ir

opt-18 -S --O2 $1.ir >$1.ll
llc-18 -O2 -filetype=obj $1.ll -o $1.o
llc-18 -O2 -filetype=asm $1.ll -o $1.s
clang-18 -o $1.bin $1.o -lm
