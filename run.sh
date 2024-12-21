#!/bin/bash

set -e
set -o pipefail

FILENAME=$1
shift

./build/bin/haven --O3 --verbose --debug-ast --emit-ir ${FILENAME} "$@" 2> >(tee log.log >&2)
clang-18 -c -o ${FILENAME%.*}.o ${FILENAME%.*}.ll
objdump -S ${FILENAME%.*}.o >${FILENAME%.*}.s
clang-18 -o ${FILENAME%.*} ${FILENAME%.*}.o -lm

time ${FILENAME%.*}
