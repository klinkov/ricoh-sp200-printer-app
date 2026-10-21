#!/bin/sh
# Regenerates golden PBM inputs and reference JBIG outputs.
# Reference = pbmtojbg -p 72 -o 3 -m 0 -q (jbigkit). Deterministic, no external images.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
jbig=${JBIG_PREFIX:-$(brew --prefix jbigkit 2>/dev/null || echo /opt/homebrew/opt/jbigkit)}
pbmtojbg=$jbig/bin/pbmtojbg
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-clang} -std=c11 -Wall -Wextra -Werror -O2 "$here/gen-pbm.c" -o "$tmp/gen-pbm"
for k in text photo odd-width; do
  "$tmp/gen-pbm" "$k" > "$here/$k.pbm"
  "$pbmtojbg" -p 72 -o 3 -m 0 -q "$here/$k.pbm" "$here/$k.jbg"
done
ls -l "$here"/*.pbm "$here"/*.jbg
