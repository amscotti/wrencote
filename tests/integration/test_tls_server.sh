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
"$build/wrencote" run tests/data/http_app.wren --listen 127.0.0.1:18097 --allow http \
    --tls-cert "$directory/cert.pem" --tls-key "$directory/key.pem" --no-sandbox &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsk --http1.1 https://127.0.0.1:18097/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -fsk --http1.1 https://127.0.0.1:18097/health)" = "ok"
echo | openssl s_client -connect 127.0.0.1:18097 -alpn http/1.1 2>/dev/null |
    grep -q "ALPN protocol: http/1.1"
echo "tls: HTTPS HTTP/1.1 and ALPN fallback ok"
