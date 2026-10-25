#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
pid=""
fuser -k 18210/tcp >/dev/null 2>&1 || true
cleanup() { if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; }
trap cleanup EXIT
"$build/wrencote" run tests/data/sse_app.wren --listen 127.0.0.1:18210 \
    --allow net,sys --no-sandbox >/tmp/wc-sse.log 2>&1 &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:18210/details >/dev/null 2>&1; then break; fi
    sleep 0.1
done
curl -fsS http://127.0.0.1:18210/details | grep -q "SSE is connected"
for path in bad-sse-start bad-sse-header bad-sse-send bad-sse-close bad-sleep; do
    curl -sS --max-time 2 -o /dev/null "http://127.0.0.1:18210/$path" || true
done
code="$(curl -sS --max-time 2 -o /dev/null -w '%{http_code}' http://127.0.0.1:18210/bad-http-scheme || true)"
test "$code" = "500"
curl -sS --max-time 0.4 http://127.0.0.1:18210/events >/dev/null 2>&1 || true
sleep 0.2
curl -fsS http://127.0.0.1:18210/details >/dev/null
curl -fsSN http://127.0.0.1:18210/events >/tmp/wc-sse-events.txt
grep -q "event: reset" /tmp/wc-sse-events.txt
grep -q '"percent":100' /tmp/wc-sse-events.txt
curl -sS --max-time 1 http://127.0.0.1:18210/stop >/dev/null 2>&1 || true
wait "$pid" 2>/dev/null || true
pid=""
echo "sse: progress, reset, and disconnect survival ok"
