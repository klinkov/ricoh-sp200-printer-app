#!/bin/sh
# Integration test: ricoh-sp200-printer-app (PAPPL) -> socket://127.0.0.1:9101 -> mock-sink -> check-ddst.
# Run via `make integration` (builds everything first). Uses ports 8011 (server) and 9101 (mock).
# Never talks to a real printer.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/build"
APP="$B/ricoh-sp200-printer-app"
IT="$B/integration"
PORT=8011
MOCK_PORT=9101
URI="ipp://localhost:$PORT/ipp/print/test"

for f in "$APP" "$IT/mock-sink" "$IT/mkpwg" "$IT/check-ddst"; do
  [ -x "$f" ] || { echo "run.sh: missing $f (run: make integration)"; exit 2; }
done

# Short path: the PAPPL domain socket lives in $TMPDIR (sun_path is limited to 104 bytes).
WORK=$(mktemp -d /tmp/ricoh-it.XXXXXX)
mkdir -p "$WORK/home"
# Isolate PAPPL state/config (~/Library/Application Support/...) and the domain socket.
export HOME="$WORK/home" TMPDIR="$WORK"

SPID=""
MPID=""
cleanup() {
  [ -n "$MPID" ] && kill "$MPID" 2>/dev/null
  if [ -n "$SPID" ]; then
    kill "$SPID" 2>/dev/null
    i=0; while kill -0 "$SPID" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    kill -9 "$SPID" 2>/dev/null
  fi
}
trap cleanup EXIT INT TERM

PASS=0
FAILN=0
ok()   { echo "PASS: $*"; PASS=$((PASS+1)); }
bad()  { echo "FAIL: $*"; FAILN=$((FAILN+1)); }

"$IT/mkpwg" 4961 "$WORK/page3.pwg" >"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 4800 "$WORK/page3-4800.pwg" >>"$WORK/mkpwg.out" || exit 1
cat "$WORK/mkpwg.out"
# Raster height from cupsRasterInitPWGHeader (A4 -> 7015 rows; the paper is 7016: bottom row padded white).
ROWS=$(sed -n '1s/.*x\([0-9]*\) 8-bit.*/\1/p' "$WORK/mkpwg.out")

"$APP" server -o server-port=$PORT -o log-file="$WORK/server.log" -o log-level=debug >"$WORK/server.out" 2>&1 &
SPID=$!
i=0
until curl -fs -o /dev/null "http://localhost:$PORT/"; do
  i=$((i+1))
  if [ $i -gt 100 ] || ! kill -0 "$SPID" 2>/dev/null; then
    echo "run.sh: server did not start"; cat "$WORK/server.out"; exit 1
  fi
  sleep 0.1
done
"$APP" add -d test -m ricoh-sp200 -v "socket://127.0.0.1:$MOCK_PORT" || { echo "run.sh: add printer failed"; exit 1; }

# print_job NAME PWG [mock args...] -> sets STATE (job-state keyword), output in $WORK/NAME.ddst.
# Env: COPIES (default 1), MDH (multiple-document-handling, default uncollated).
print_job() {
  name=$1 pwg=$2
  shift 2
  out="$WORK/$name.ddst"
  rm -f "$out" "$WORK/$name.mock"
  "$IT/mock-sink" "$@" $MOCK_PORT "$out" 2>"$WORK/$name.mock" &
  MPID=$!
  i=0
  until grep -q listening "$WORK/$name.mock" 2>/dev/null; do
    i=$((i+1)); [ $i -gt 50 ] && break; sleep 0.1
  done
  ipptool -tv -f "$pwg" -d media=iso_a4_210x297mm -d copies="${COPIES:-1}" \
    -d mdh="${MDH:-separate-documents-uncollated-copies}" "$URI" "$ROOT/tests/integration/print-job.test" >"$WORK/$name.ipp" 2>&1
  i=0
  while kill -0 "$MPID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
  kill "$MPID" 2>/dev/null
  MPID=""
  STATE=$(sed -n 's/.*job-state (enum) = \([a-z-]*\).*/\1/p' "$WORK/$name.ipp" | tail -1)
  echo "== $name: job-state=${STATE:-?}; $(tail -1 "$WORK/$name.mock")"
}

alive() {
  kill -0 "$SPID" 2>/dev/null && curl -fs -o /dev/null "http://localhost:$PORT/"
}

# 1. Full-width A4 raster: 3 pages, one EOJ, IMAGELEN == data, PAPERWIDTH=4961, right band black.
print_job full "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "full: job completed" || bad "full: job-state=$STATE"
"$IT/check-ddst" "$WORK/full.ddst" 3 1 1 4961 "$ROWS" 4945 4961 && ok "full: DDST stream" || bad "full: DDST stream"

# 2. Narrow raster (cupsWidth=4800): rows padded with white to PAPERWIDTH=4961, not clipped (Review Focus 4).
print_job narrow "$WORK/page3-4800.pwg"
[ "$STATE" = completed ] && ok "narrow: job completed" || bad "narrow: job-state=$STATE"
"$IT/check-ddst" "$WORK/narrow.ddst" 3 1 1 4961 "$ROWS" 4784 4800 && ok "narrow: padded to 4961, band intact" || bad "narrow: padding"
grep -q "raster 4800x${ROWS}x8 -> PAPER=A4 4961x7016 (padded right" "$WORK/server.log" && ok "narrow: padding logged" || bad "narrow: padding not logged"

# 3. Printer drops the connection after 5000 bytes: job aborted, service alive (Review Focus 3).
print_job drop "$WORK/page3.pwg" --close-after 5000
[ "$STATE" = aborted ] && ok "drop: job aborted" || bad "drop: job-state=$STATE (want aborted)"
alive && ok "drop: service alive" || bad "drop: service died"

# 4. Printing works again after the failure.
print_job again "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "again: job completed" || bad "again: job-state=$STATE"
"$IT/check-ddst" "$WORK/again.ddst" 3 1 1 4961 "$ROWS" 4945 4961 && ok "again: DDST stream" || bad "again: DDST stream"

# 5. copies=2, uncollated: printer makes the copies (COPIES=2 per page, one job).
COPIES=2 print_job copies "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "copies: job completed" || bad "copies: job-state=$STATE"
"$IT/check-ddst" "$WORK/copies.ddst" 3 1 2 4961 "$ROWS" 4945 4961 && ok "copies: COPIES=2 on 3 pages, 1 job" || bad "copies"

# 6. copies=2, collated: the service replays the job (2 PJL jobs with COPIES=1, spec §10).
COPIES=2 MDH=separate-documents-collated-copies print_job collated "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "collated: job completed" || bad "collated: job-state=$STATE"
"$IT/check-ddst" "$WORK/collated.ddst" 6 2 1 4961 "$ROWS" 4945 4961 && ok "collated: 2 jobs x 3 pages, COPIES=1" || bad "collated"

alive && ok "service still alive" || bad "service died"

echo "== integration: $PASS passed, $FAILN failed (logs: $WORK)"
if [ $FAILN -ne 0 ]; then
  echo "--- server.log (errors) ---"
  grep -E "\[(Job|Printer).*(E|W)\]|ERROR|error" "$WORK/server.log" | tail -30
  exit 1
fi
rm -rf "$WORK"
exit 0
