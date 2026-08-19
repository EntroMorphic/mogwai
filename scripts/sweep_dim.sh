#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
echo "  dim   representation        iot acc        wrong  missed  index KB"
for D in 128 256 512 1024 2048; do
  cc -std=c11 -O2 -DRD=$D -o c/bin/sweep c/src/compare.c c/src/router.c \
     c/src/ternary.c c/src/cascade.c 2>/dev/null || { echo "  $D  build failed"; continue; }
  ./c/bin/sweep data/train.json data/validation.json data/test.json data/nlu_home.csv 2>/dev/null \
    | grep -E "binary|twin-ternary" | sed "s/^/  $D  /"
done
