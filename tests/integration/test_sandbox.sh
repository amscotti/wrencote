#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
root="$(mktemp -d)"
pid=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$root"
}
trap cleanup EXIT

printf 'file-ok\n' >"$root/ok.txt"
"$build/wrencote" run tests/data/sandbox_app.wren --listen 127.0.0.1:18094 \
    --root "$root" --allow http,fs --sandbox &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:18094/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -fsS http://127.0.0.1:18094/health)" = "ok"
test "$(curl -fsS http://127.0.0.1:18094/file)" = "file-ok"
test "$(curl -fsS http://127.0.0.1:18094/escape)" = "denied"
test "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:18094/nope)" = "404"
echo "sandbox: serving, fs root, and escape denial ok"
