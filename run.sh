#!/bin/bash

set -e
set -o pipefail

./build/bin/haven --verbose --debug-ast --emit-ir $1 2> >(tee -a log.log >&2)
clang-18 -O2 -c -o ${1%.*}.o ${1%.*}.ll
clang-18 -O2 -S -emit-llvm -o ${1%.*}.opt.ll ${1%.*}.ll
objdump -S ${1%.*}.o >${1%.*}.s
clang-18 -o ${1%.*} ${1%.*}.o -lm

time ${1%.*}
