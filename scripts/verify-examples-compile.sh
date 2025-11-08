#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(dirname "${SCRIPT_DIR}")

if [ ! -x ${REPO_DIR}/build/bin/haven ]; then
    echo "Expected a built Haven compiler in build/bin - build the compiler first."
    exit 1
fi

FAILURES=()

for f in "${REPO_DIR}"/examples/*.hv; do
    # Only git-tracked examples
    git ls-files --error-unmatch "${f}" >/dev/null 2>&1 || continue

    if ! ./build/bin/haven -I /usr/include -c "${f}" -o /dev/null; then
        FAILURES+=("${f}")
    fi
done

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo "These examples failed to compile:"
    echo ${FAILURES[@]}
fi
