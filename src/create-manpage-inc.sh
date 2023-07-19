#!/usr/bin/env bash

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <man-in> <inc-out>"
    exit 1
fi

INPUT=$1
OUTPUT=$2

cat > "$OUTPUT" <<EOF
/* created with create-manpage-inc.sh from $INPUT */

static constexpr char kGzippedManpage[] =
EOF
man -Tascii "${INPUT}" | gzip -9 | od -tx1 -Anone \
    | sed 's/ \([0-9A-Fa-f][0-9A-Fa-f]\)/\\x\1/g' \
    | awk '{  printf("    \"%s\"\n", $0); } END { printf(";\n"); }' \
	  >> "${OUTPUT}"
wc -l "${OUTPUT}"
