#!/bin/bash

set -e

FORMAT_OUT=${TMPDIR:-/tmp}/clang-format-diff.out

find src -name "*.h" -o -name "*.cc" | xargs clang-format-13 --verbose -i

# Check if we got any diff, then print it out in in the CI.
# TODO: make these suggested diffs in the pull request.
git diff > ${FORMAT_OUT}

if [ -s ${FORMAT_OUT} ]; then
    echo "== There were changes running the formatter =="
    cat ${FORMAT_OUT}
    echo "To locally fix, run .github/bin/run-clang-format.sh then commit and push."
    exit 1
fi

exit 0
