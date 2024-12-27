#!/bin/bash

set -e
set -o pipefail

FILENAME=$1
shift

COMPILER="./build/bin/haven"
# COMPILER="./build/bin/haven_bootstrap"

OPT="O2"
ASAN="--asan"
#ASAN=""

${COMPILER} --${OPT} ${ASAN} --verbose --debug-ast --debug-ir -I /usr/include ${FILENAME} "$@" 2> >(tee log.log >&2) 1>&2

set +e

time ${FILENAME%.*}
echo "Exited with $?" 1>&2
