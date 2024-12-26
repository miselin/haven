#!/bin/bash

set -e
set -o pipefail

FILENAME=$1
shift

COMPILER="./build/bin/haven"
# COMPILER="./build/bin/haven_bootstrap"

OPT="O0"
ASAN="-fsanitize=address"
#ASAN=""

${COMPILER} --${OPT} --verbose --debug-ast --debug-ir --emit-ir -I /usr/include ${FILENAME} "$@" 2> >(tee log.log >&2) 1>&2
clang-18 -S -${OPT} -g -o ${FILENAME%.*}.s ${FILENAME%.*}.ll -lm
clang-18 -L build/lib -${OPT} -g ${ASAN} -o ${FILENAME%.*} ${FILENAME%.*}.ll -lm -lruntime

set +e

time ${FILENAME%.*}
echo "Exited with $?" 1>&2
