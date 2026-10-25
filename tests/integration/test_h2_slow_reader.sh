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
"$build/wrencote" run tests/data/http_app.wren --listen 127.0.0.1:18100 --allow http \
    --tls-cert "$directory/cert.pem" --tls-key "$directory/key.pem" --no-sandbox &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsk --http2 https://127.0.0.1:18100/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
python3 tests/integration/test_h2_slow_reader.py 18100
python3 tests/integration/test_h2_p99.py 18100
curl -fsk --http2 https://127.0.0.1:18100/stop >/dev/null 2>&1 || true
wait "$pid" 2>/dev/null || true
pid=""
echo "h2 slow reader: isolation ok"
