#!/bin/bash

set -e
set -o pipefail

FILENAME=$1
shift

./build/bin/haven --verbose --debug-ast --emit-ir ${FILENAME} "$@" 2> >(tee log.log >&2)
clang-18 -O2 -c -o ${FILENAME%.*}.o ${FILENAME%.*}.ll
clang-18 -O2 -S -emit-llvm -o ${FILENAME%.*}.opt.ll ${FILENAME%.*}.ll
objdump -S ${FILENAME%.*}.o >${FILENAME%.*}.s
clang-18 -o ${FILENAME%.*} ${FILENAME%.*}.o -lm

time ${FILENAME%.*}
