#!/bin/sh
# Tools test: mkgrid -> pbm2ddst; checks page count and IMAGELEN. Usage: test_tools.sh [build/tools]
set -e
B=${1:-build/tools}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"$B/mkgrid" > "$T/grid.pbm"
"$B/pbm2ddst" "$T/grid.pbm" > "$T/grid.ddst"
n=$(grep -a -c 'PAGESTATUS=END' "$T/grid.ddst")
[ "$n" = 1 ] || { echo "FAIL: PAGESTATUS=END count $n"; exit 1; }
n=$(grep -a -c 'PAGESTATUS=START' "$T/grid.ddst")
[ "$n" = 1 ] || { echo "FAIL: PAGESTATUS=START count $n"; exit 1; }
python3 -I - "$T/grid.ddst" <<'PY'
import re, sys
d = open(sys.argv[1], 'rb').read()
m = re.search(rb'@PJL SET IMAGELEN=(\d+)\r\n', d)
assert m, 'no IMAGELEN'
n = int(m.group(1))
end = d.index(b'@PJL SET DOTCOUNT=', m.end())
assert end - m.end() == n, 'IMAGELEN %d != actual %d' % (n, end - m.end())
assert d.endswith(b'@PJL EOJ\r\n\x1b%-12345X'), 'bad tail'
assert d.startswith(b'\x1b%-12345X@PJL'), 'bad head'
print('IMAGELEN ok', n)
PY
# custom geometry
"$B/mkgrid" -w 800 -h 1000 > "$T/s.pbm"
"$B/pbm2ddst" -p LETTER -c 2 -s manual -d raw "$T/s.pbm" > "$T/s.ddst" 2>/dev/null
grep -a -q 'PAPERWIDTH=800' "$T/s.ddst" && grep -a -q 'COPIES=2' "$T/s.ddst" && grep -a -q 'MEDIASOURCE=MANUAL' "$T/s.ddst"
if "$B/pbm2ddst" -p NOPE "$T/s.pbm" >/dev/null 2>&1; then echo "FAIL: bad paper accepted"; exit 1; fi
echo "test_tools: PASS"
