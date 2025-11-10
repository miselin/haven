#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(dirname "${SCRIPT_DIR}")

FAILURES=()

for f in "${REPO_DIR}"/examples/*.hv; do
    # Only git-tracked examples
    git ls-files --error-unmatch "${f}" >/dev/null 2>&1 || continue

    # TODO: validate.py should live in the scripts/ dir
    if ! python3 docs/validate.py "${f}" >/dev/null; then
        FAILURES+=("${f}")
    fi
done

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo "These examples failed to parse with the Lark grammar:"
    echo ${FAILURES[@]}
fi
