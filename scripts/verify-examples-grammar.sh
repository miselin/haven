#!/bin/bash

# Usage: verify-examples-grammar.sh <path-to-haven.lark>

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(dirname "${SCRIPT_DIR}")

FAILURES=()
EXPECTED_FAILURES="${REPO_DIR}/examples/badlex.hv ${REPO_DIR}/examples/missing_expr.hv"

ls -la ${1}

for f in "${REPO_DIR}"/examples/*.hv; do
    echo "${EXPECTED_FAILURES}" | grep "${f}" >/dev/null && continue

    if ! python3 "${SCRIPT_DIR}"/validate-lark.py "${1}" "${f}" >/dev/null; then
        FAILURES+=("${f}")
    fi
done

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo "These examples failed to parse with the Lark grammar:"
    echo ${FAILURES[@]}
    exit 1
fi
