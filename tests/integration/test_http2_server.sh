#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
directory="$(mktemp -d)"
pid=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$directory"
}
trap cleanup EXIT

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
    -keyout "$directory/key.pem" -out "$directory/cert.pem" >/dev/null 2>&1
"$build/wrencote" run tests/data/http_app.wren --listen 127.0.0.1:18098 --allow http \
    --tls-cert "$directory/cert.pem" --tls-key "$directory/key.pem" --no-sandbox &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsk --http2 https://127.0.0.1:18098/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -fsk --http2 https://127.0.0.1:18098/health)" = "ok"
test "$(curl -fsk --http2 'https://127.0.0.1:18098/inspect?a=1&b=2' -H 'x-probe: p1')" = "GET|2|false|1|2|p1"
test "$(curl -fsk --http2 https://127.0.0.1:18098/headers -H 'x-probe: hello1')" = "hello1"
curl -fsk --http2 -D - https://127.0.0.1:18098/health -o /dev/null | grep -qi 'x-test: yes'
curl -fsk --http2 -D - https://127.0.0.1:18098/json -o /dev/null | grep -qi 'content-type: application/json' 
test "$(curl -fsk --http1.1 https://127.0.0.1:18098/health)" = "ok"
curl -fsk --http2 https://127.0.0.1:18098/stop >/dev/null 2>&1 || true
wait "$pid" 2>/dev/null || true
pid=""
echo "http2: h2 and HTTP/1.1 fallback served"
