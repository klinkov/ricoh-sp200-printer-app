#!/bin/sh
# Integration test: ricoh-sp200-printer-app (PAPPL) -> ricoh-socket://127.0.0.1:9101 -> mock-sink -> check-ddst.
# Run via `make integration` (builds everything first). Uses ports 8011 (server, localhost only) and 9101
# (mock). Never talks to a real printer.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/build"
APP="$B/ricoh-sp200-printer-app"
IT="$B/integration"
TD="$ROOT/tests/integration"
PORT=8011
MOCK_PORT=9101
URI="ipp://localhost:$PORT/ipp/print/test"
PLAIN_URI="ipp://localhost:$PORT/ipp/print/plain"
A4=iso_a4_210x297mm

for f in "$APP" "$IT/mock-sink" "$IT/mkpwg" "$IT/mkpng" "$IT/check-ddst" "$IT/test-autoadd"; do
  [ -x "$f" ] || { echo "run.sh: missing $f (run: make integration)"; exit 2; }
done

# Short path: the PAPPL domain socket lives in $TMPDIR (sun_path is limited to 104 bytes).
WORK=$(mktemp -d /tmp/ricoh-it.XXXXXX)
mkdir -p "$WORK/home"
# Isolate PAPPL state/config (~/Library/Application Support/...) and the domain socket.
export HOME="$WORK/home" TMPDIR="$WORK"

private_servers() { pgrep -f "$APP server -o private-server" 2>/dev/null | sort; }
PRIV_BEFORE=$(private_servers)

SPID=""
MPID=""
BGPIDS=""
cleanup() {
  for p in $BGPIDS; do kill "$p" 2>/dev/null; done
  [ -n "$MPID" ] && kill "$MPID" 2>/dev/null
  if [ -n "$SPID" ]; then
    kill "$SPID" 2>/dev/null
    i=0; while kill -0 "$SPID" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    kill -9 "$SPID" 2>/dev/null
  fi
  SPID=""; MPID=""; BGPIDS=""
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

PASS=0
FAILN=0
ok()   { echo "PASS: $*"; PASS=$((PASS+1)); }
bad()  { echo "FAIL: $*"; FAILN=$((FAILN+1)); }

# 0. Driver auto-selection (unit test of ricoh_autoadd_cb).
"$IT/test-autoadd" >"$WORK/autoadd.out" 2>&1
cat "$WORK/autoadd.out"
PASS=$((PASS + $(grep -c '^PASS:' "$WORK/autoadd.out")))
FAILN=$((FAILN + $(grep -c '^FAIL:' "$WORK/autoadd.out")))

"$IT/mkpwg" 4961 "$WORK/page3.pwg" >"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 4920 "$WORK/page3-4920.pwg" >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 0 "$WORK/page3-real.pwg" >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 4800 "$WORK/page1-4800.pwg" $A4 1 >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 0 "$WORK/a5.pwg" iso_a5_148x210mm 1 >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 0 "$WORK/gray.pwg" $A4 1 gray127 >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 0 "$WORK/text.pwg" $A4 1 text >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpwg" 4961 "$WORK/page5.pwg" $A4 5 >>"$WORK/mkpwg.out" || exit 1
"$IT/mkpng" "$WORK/square.png" 400 566 || exit 1
cat "$WORK/mkpwg.out"
# Raster height from cupsRasterInitPWGHeader (A4 -> 7015 rows; the paper is 7016: bottom row padded white).
ROWS=$(sed -n '1s/.*x\([0-9]*\) 8-bit.*/\1/p' "$WORK/mkpwg.out")
REALW=$(sed -n 's/.*page3-real.pwg: 3 pages \([0-9]*\)x.*/\1/p' "$WORK/mkpwg.out")
A5W=$(sed -n 's/.*a5.pwg: 1 pages \([0-9]*\)x.*/\1/p' "$WORK/mkpwg.out")
A5ROWS=$(sed -n 's/.*a5.pwg: 1 pages [0-9]*x\([0-9]*\) .*/\1/p' "$WORK/mkpwg.out")

# A small collate limit so that a 5-page job (~10 MB) overflows it but 3 pages (~6 MB) do not.
RICOH_COLLATE_MAX_BYTES=8000000 "$APP" server -o server-port=$PORT -o listen-hostname=localhost \
  -o log-file="$WORK/server.log" -o log-level=debug >"$WORK/server.out" 2>&1 &
SPID=$!
i=0
until curl -fs -o /dev/null "http://localhost:$PORT/"; do
  i=$((i+1))
  if [ $i -gt 100 ] || ! kill -0 "$SPID" 2>/dev/null; then
    echo "run.sh: server did not start"; cat "$WORK/server.out"; exit 1
  fi
  sleep 0.1
done
"$APP" add -d test -m ricoh-sp200 -v "ricoh-socket://127.0.0.1:$MOCK_PORT" || { echo "run.sh: add printer failed"; exit 1; }
"$APP" add -d plain -m ricoh-sp200 -v "socket://127.0.0.1:$MOCK_PORT" || { echo "run.sh: add printer failed"; exit 1; }

# start_mock NAME [mock args...]: starts mock-sink writing $WORK/NAME.ddst; false if it did not come up.
start_mock() {
  name=$1
  shift
  rm -f "$WORK/$name.ddst" "$WORK/$name.ddst."* "$WORK/$name.mock"
  "$IT/mock-sink" "$@" $MOCK_PORT "$WORK/$name.ddst" 2>"$WORK/$name.mock" &
  MPID=$!
  i=0
  until grep -q listening "$WORK/$name.mock" 2>/dev/null; do
    i=$((i+1)); [ $i -gt 50 ] && break
    kill -0 "$MPID" 2>/dev/null || break
    sleep 0.1
  done
  if ! kill -0 "$MPID" 2>/dev/null || ! grep -q listening "$WORK/$name.mock"; then
    bad "$name: mock-sink did not start: $(cat "$WORK/$name.mock")"
    MPID=""
    return 1
  fi
}

# stop_mock: wait up to 10 s for mock-sink to exit by itself, then kill it.
stop_mock() {
  i=0
  while [ -n "$MPID" ] && kill -0 "$MPID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
  [ -n "$MPID" ] && kill "$MPID" 2>/dev/null
  MPID=""
}

# ipp_job NAME FILE: submit (and, unless NOWAIT is set, wait for) a job; output in $WORK/NAME.ipp.
# Env: Q (printer URI, default $URI), FORMAT (default image/pwg-raster), COPIES (1), MDH (unset ->
# attribute omitted), CONTENT (auto), MEDIA ($A4), NOWAIT. CONTENT=photo makes the gradient pages
# Floyd-Steinberg dithered (~2 MB of JBIG per page) where a case needs a large stream.
ipp_job() {
  ipptool -T 60 -tv -f "$2" -d format="${FORMAT:-image/pwg-raster}" -d media="${MEDIA:-$A4}" \
    -d copies="${COPIES:-1}" -d content="${CONTENT:-auto}" ${MDH:+-d mdh="$MDH"} ${NOWAIT:+-d nowait=1} \
    "${Q:-$URI}" "$TD/print-job.test" >"$WORK/$1.ipp" 2>&1
  # sh keeps "VAR=x func" assignments after the call: reset them so they never leak into the next case.
  unset Q FORMAT COPIES MDH CONTENT MEDIA NOWAIT
}

job_state() { sed -n 's/.*job-state (enum) = \([a-z-]*\).*/\1/p' "$1" | tail -1; }

# print_job NAME FILE [mock args...] -> sets STATE (job-state keyword), output in $WORK/NAME.ddst.
print_job() {
  name=$1 file=$2
  shift 2
  STATE=""
  start_mock "$name" "$@" || return 1
  ipp_job "$name" "$file"
  stop_mock
  STATE=$(job_state "$WORK/$name.ipp")
  echo "== $name: job-state=${STATE:-?}; $(tail -1 "$WORK/$name.mock")"
}

alive() {
  kill -0 "$SPID" 2>/dev/null && curl -fs -o /dev/null "http://localhost:$PORT/"
}

size_of() { wc -c <"$1" | tr -d ' '; }

# 1. Full-width A4 raster: 3 pages, one EOJ, IMAGELEN == data, DOTCOUNT, PAPERWIDTH=4961, right band black.
print_job full "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "full: job completed" || bad "full: job-state=$STATE"
"$IT/check-ddst" "$WORK/full.ddst" 3 1 1 4961 "$ROWS" 4945 4961 && ok "full: DDST stream" || bad "full: DDST stream"

# 2. Slightly narrow raster (4920 = 1.7 mm < A4): rows padded with white to PAPERWIDTH=4961, not clipped.
print_job narrow "$WORK/page3-4920.pwg"
[ "$STATE" = completed ] && ok "narrow: job completed" || bad "narrow: job-state=$STATE"
"$IT/check-ddst" "$WORK/narrow.ddst" 3 1 1 4961 "$ROWS" 4904 4920 && ok "narrow: padded to 4961, band intact" || bad "narrow: padding"
grep -q "raster 4920x${ROWS}x8 -> PAPER=A4 4961x7016 (padded right" "$WORK/server.log" && ok "narrow: padding logged" || bad "narrow: padding not logged"

# 3. A4 raster exactly as cupsRasterInitPWGHeader makes it (what real clients send).
print_job real "$WORK/page3-real.pwg"
[ "$STATE" = completed ] && ok "real-width: job completed (raster width $REALW)" || bad "real-width: job-state=$STATE"
"$IT/check-ddst" "$WORK/real.ddst" 3 1 1 4961 "$ROWS" $((REALW - 16)) "$REALW" && ok "real-width: band at $((REALW - 16))..$((REALW - 1)), padded to 4961" || bad "real-width: DDST stream"

# 4. Printer drops the connection after 5000 bytes: job aborted, service alive.
CONTENT=photo print_job drop "$WORK/page3.pwg" --close-after 5000
[ "$STATE" = aborted ] && ok "drop: job aborted" || bad "drop: job-state=$STATE (want aborted)"
alive && ok "drop: service alive" || bad "drop: service died"

# 5. Printing works again after the failure.
print_job again "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "again: job completed" || bad "again: job-state=$STATE"
"$IT/check-ddst" "$WORK/again.ddst" 3 1 1 4961 "$ROWS" 4945 4961 && ok "again: DDST stream" || bad "again: DDST stream"

# 6. copies=2, explicitly uncollated: printer makes the copies (COPIES=2 per page, one job).
COPIES=2 MDH=separate-documents-uncollated-copies print_job copies "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "copies: job completed" || bad "copies: job-state=$STATE"
"$IT/check-ddst" "$WORK/copies.ddst" 3 1 2 4961 "$ROWS" 4945 4961 && ok "copies: COPIES=2 on 3 pages, 1 job" || bad "copies"

# 7. copies=2, collated: the service replays the job (2 PJL jobs with COPIES=1, spec §10).
COPIES=2 MDH=separate-documents-collated-copies print_job collated "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "collated: job completed" || bad "collated: job-state=$STATE"
"$IT/check-ddst" "$WORK/collated.ddst" 6 2 1 4961 "$ROWS" 4945 4961 && ok "collated: 2 jobs x 3 pages, COPIES=1" || bad "collated"

# 8. copies=2 without multiple-document-handling: collated (PAPPL's default).
COPIES=2 print_job nomdh "$WORK/page3.pwg"
[ "$STATE" = completed ] && ok "no-mdh: job completed" || bad "no-mdh: job-state=$STATE"
"$IT/check-ddst" "$WORK/nomdh.ddst" 6 2 1 4961 "$ROWS" 4945 4961 && ok "no-mdh: collated, 2 jobs x 3 pages" || bad "no-mdh: not collated"

# 9. Collate buffer over the limit (8 MB here): falls back to printer copies, one job, COPIES=2 everywhere.
CONTENT=photo COPIES=2 print_job bigcollate "$WORK/page5.pwg"
[ "$STATE" = completed ] && ok "collate-limit: job completed" || bad "collate-limit: job-state=$STATE"
"$IT/check-ddst" "$WORK/bigcollate.ddst" 5 1 2 4961 "$ROWS" 4945 4961 && ok "collate-limit: 1 job x 5 pages, COPIES=2" || bad "collate-limit: stream"
grep -q "uncollated copies" "$WORK/bigcollate.ipp" && grep -q "switching to printer copies" "$WORK/server.log" \
  && ok "collate-limit: job-state-message + log warning" || bad "collate-limit: no message/warning"

# 10. PNG with copies=2: PAPPL's image filter repeats the page itself -> 2 pages with COPIES=1.
FORMAT=image/png COPIES=2 print_job png "$WORK/square.png"
[ "$STATE" = completed ] && ok "png-copies: job completed" || bad "png-copies: job-state=$STATE"
"$IT/check-ddst" "$WORK/png.ddst" 2 1 1 4961 7016 4961 4961 && ok "png-copies: 2 pages, COPIES=1, 1 job" || bad "png-copies: stream"

# 11. A5 raster on an A4 job: the raster size wins (PAPER=A5).
print_job a5 "$WORK/a5.pwg"
[ "$STATE" = completed ] && ok "a5-raster: job completed" || bad "a5-raster: job-state=$STATE"
"$IT/check-ddst" "$WORK/a5.ddst" 1 1 1 3496 "$A5ROWS" $((A5W - 16)) "$A5W" && grep -aq "@PJL SET PAPER=A5" "$WORK/a5.ddst" \
  && ok "a5-raster: PAPER=A5, PAPERWIDTH=3496" || bad "a5-raster: stream"

# 12. Raster that matches neither the media nor any paper: aborted with an error.
print_job mismatch "$WORK/page1-4800.pwg"
[ "$STATE" = aborted ] && grep -q "matches neither media" "$WORK/mismatch.ipp" \
  && ok "size-mismatch: job aborted with message" || bad "size-mismatch: job-state=$STATE"

# 13. Default dithering on a flat 50% gray: threshold (all black), no Floyd-Steinberg noise.
print_job gray "$WORK/gray.pwg"
[ "$STATE" = completed ] && ok "gray-auto: job completed" || bad "gray-auto: job-state=$STATE"
"$IT/check-ddst" "$WORK/gray.ddst" 1 1 1 4961 "$ROWS" 0 "$REALW" && ok "gray-auto: solid, no dither noise" || bad "gray-auto: dithered"

# 14. print-content-optimize=photo: Floyd-Steinberg (the same check must find white dots).
CONTENT=photo print_job grayphoto "$WORK/gray.pwg"
[ "$STATE" = completed ] && ok "gray-photo: job completed" || bad "gray-photo: job-state=$STATE"
"$IT/check-ddst" "$WORK/grayphoto.ddst" 1 1 1 4961 "$ROWS" 0 "$REALW" >/dev/null 2>&1 \
  && bad "gray-photo: not dithered" || ok "gray-photo: dithered"

# 15. Small text job, then the same job with the printer dropping the last 10 bytes: aborted.
print_job small "$WORK/text.pwg"
[ "$STATE" = completed ] && ok "small: job completed" || bad "small: job-state=$STATE"
"$IT/check-ddst" "$WORK/small.ddst" 1 1 1 4961 "$ROWS" 4961 4961 && ok "small: DDST stream" || bad "small: DDST stream"
SMALL=$(size_of "$WORK/small.ddst")
print_job tail "$WORK/text.pwg" --close-after $((SMALL - 10))
[ "$STATE" = aborted ] && ok "tail-drop: job aborted ($SMALL-byte job, printer took $((SMALL - 10)))" || bad "tail-drop: job-state=$STATE (want aborted)"
alive && ok "tail-drop: service alive" || bad "tail-drop: service died"

# 16. Two queued jobs, the printer drops the first: the second one still prints (fresh connection).
ipptool -T 60 -tv -d pause=1 "$URI" "$TD/queue-ctl.test" >"$WORK/pause.ipp" 2>&1 || bad "queue: Pause-Printer"
if start_mock queue --close-after 5000 --conns 2; then
  FORMAT=image/png NOWAIT=1 ipp_job queue1 "$WORK/square.png"
  FORMAT=image/png NOWAIT=1 ipp_job queue2 "$WORK/square.png"
  J1=$(sed -n 's/.*job-id (integer) = \([0-9]*\).*/\1/p' "$WORK/queue1.ipp" | head -1)
  J2=$(sed -n 's/.*job-id (integer) = \([0-9]*\).*/\1/p' "$WORK/queue2.ipp" | head -1)
  ipptool -T 60 -tv -d resume=1 "$URI" "$TD/queue-ctl.test" >"$WORK/resume.ipp" 2>&1 || bad "queue: Resume-Printer"
  ipptool -T 60 -tv -d wait="${J1:-0}" "$URI" "$TD/queue-ctl.test" >"$WORK/queue1-wait.ipp" 2>&1
  ipptool -T 60 -tv -d wait="${J2:-0}" "$URI" "$TD/queue-ctl.test" >"$WORK/queue2-wait.ipp" 2>&1
  stop_mock
  S1=$(job_state "$WORK/queue1-wait.ipp")
  S2=$(job_state "$WORK/queue2-wait.ipp")
  # PAPPL picks the queued jobs in its own order: the first one started gets the dropped connection.
  FIRST=$(sed -n 's/.*\[Job \([0-9]*\)\] Starting print job.*/\1/p' "$WORK/server.log" | grep -x -e "${J1:-x}" -e "${J2:-x}" | head -1)
  if [ "$FIRST" = "$J1" ]; then SF=$S1; SS=$S2; else SF=$S2; SS=$S1; fi
  echo "== queue: job $J1 $S1, job $J2 $S2 (job $FIRST ran first); $(tr '\n' ' ' <"$WORK/queue.mock")"
  [ "$SF" = aborted ] && ok "queue: first job aborted" || bad "queue: first job-state=$SF"
  [ "$SS" = completed ] && ok "queue: second job completed" || bad "queue: second job-state=$SS"
  # One "Device write metrics" line = PAPPL kept the device open across both jobs (the sticky case).
  [ "$(grep -c -e "\[Job $J1\] Device write metrics" -e "\[Job $J2\] Device write metrics" "$WORK/server.log")" = 1 ] \
    && ok "queue: device stayed open between the jobs" || bad "queue: device was reopened (case not exercised)"
  "$IT/check-ddst" "$WORK/queue.ddst.2" 1 1 1 4961 7016 4961 4961 && ok "queue: second job's stream complete" || bad "queue: second stream"
fi

# 17. Cancel-Job while a 3-page job is printing (slow printer): canceled, stream ends EOJ + UEL.
if start_mock cancel --delay-ms 20; then
  CONTENT=photo ipp_job cancel "$WORK/page3.pwg" &
  IPID=$!
  BGPIDS="$IPID"
  i=0
  while [ ! -s "$WORK/cancel.ddst" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
  ipptool -T 60 -tv "$URI" "$TD/cancel-job.test" >"$WORK/cancel-op.ipp" 2>&1 || bad "cancel: Cancel-Job failed"
  wait "$IPID"
  BGPIDS=""
  stop_mock
  STATE=$(job_state "$WORK/cancel.ipp")
  echo "== cancel: job-state=${STATE:-?}; $(tail -1 "$WORK/cancel.mock")"
  [ "$STATE" = canceled ] && ok "cancel: job canceled" || bad "cancel: job-state=$STATE"
  "$IT/check-ddst" "$WORK/cancel.ddst" any 1 1 4961 "$ROWS" 4945 4961 && ok "cancel: stream closed with EOJ + UEL" || bad "cancel: stream"
  alive && ok "cancel: service alive" || bad "cancel: service died"
fi

# 18. Plain socket:// still works.
Q=$PLAIN_URI print_job plain "$WORK/text.pwg"
[ "$STATE" = completed ] && ok "socket://: job completed" || bad "socket://: job-state=$STATE"
"$IT/check-ddst" "$WORK/plain.ddst" 1 1 1 4961 "$ROWS" 4961 4961 && ok "socket://: DDST stream" || bad "socket://: stream"

alive && ok "service still alive" || bad "service died"

# No CLI call may have auto-started a private server (it would mean the CLI missed our server).
NEWPRIV=""
for p in $(private_servers); do
  printf '%s\n' "$PRIV_BEFORE" | grep -qx "$p" || NEWPRIV="$NEWPRIV $p"
done
if [ -z "$NEWPRIV" ]; then
  ok "no private-server left running"
else
  bad "private-server started by the test: $NEWPRIV"
  for p in $NEWPRIV; do kill "$p" 2>/dev/null; done
fi

echo "== integration: $PASS passed, $FAILN failed (logs: $WORK)"
if [ $FAILN -ne 0 ]; then
  echo "--- server.log (errors) ---"
  grep -E "\[(Job|Printer).*(E|W)\]|ERROR|error" "$WORK/server.log" | tail -30
  exit 1
fi
cleanup
rm -rf "$WORK"
exit 0
