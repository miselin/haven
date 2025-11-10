#!/bin/bash

set -eu

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(dirname "${SCRIPT_DIR}")

COMPILER=${REPO_DIR}/build/bin/haven

if [ ! -x ${COMPILER} ]; then
    COMPILER=bin/haven
    if [ ! -x ${COMPILER} ]; then
        echo "Expected a built Haven compiler in build/bin/ or bin/ - build the compiler first."
        exit 1
    fi
fi

FAILURES=()
EXPECTED_FAILURES="${REPO_DIR}/examples/badparse.hv ${REPO_DIR}/examples/badsemantic1.hv ${REPO_DIR}/examples/badsemantic2.hv ${REPO_DIR}/examples/badlex.hv ${REPO_DIR}/examples/missing_expr.hv ${REPO_DIR}/examples/impure.hv"

IFS=";"
read -ra params <<< "$@"
unset IFS

for f in "${REPO_DIR}"/examples/*.hv; do
    echo "${EXPECTED_FAILURES}" | grep "${f}" >/dev/null && continue

    if ! ${COMPILER} -c "${f}" ${params[@]} -o /dev/null; then
        FAILURES+=("${f}")
    fi
done

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo "These examples failed to compile:"
    echo ${FAILURES[@]}
    exit 1
fi
