#!/bin/bash

set -e
set -o pipefail

FILENAME=$1
shift

OPT="O2"

./build/bin/haven --${OPT} --verbose --debug-ast --debug-ir --emit-ir -I /usr/include ${FILENAME} "$@" 2> >(tee log.log >&2)
clang-18 -S -masm=intel -${OPT} -g3 -ggdb -gdwarf-2 -o ${FILENAME%.*}.s ${FILENAME%.*}.ll -lm
clang-18 -${OPT} -g3 -ggdb -gdwarf-2 -fsanitize=address -o ${FILENAME%.*} ${FILENAME%.*}.ll -lm

time ${FILENAME%.*}
