#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
bin="$build/wrencote"

"$build/wrencote" --version | grep -q "wrencote 0.2.1"
"$build/wrencote" --help | grep -q "usage:"
timeout 5 "$build/wrencote" run tests/data/print.wren --allow sys --no-listen --no-sandbox >/tmp/wc-print.out 2>&1
grep -q hello /tmp/wc-print.out
if "$build/wrencote" >/dev/null 2>&1; then
    echo "no-args should fail" >&2
    exit 1
fi
if "$build/wrencote" run examples/app.wren --allow fs >/dev/null 2>&1; then
    echo "fs without root should fail" >&2
    exit 1
fi
if "$build/wrencote" check tests/data/bad.wren >/tmp/wrencote-cli-check.log 2>&1; then
    echo "check should fail" >&2
    exit 1
fi
grep -q "bad.wren" /tmp/wrencote-cli-check.log
"$build/wrencote" check examples/app.wren --allow http,json --max-request 2048 --vm-heap 1048576 --log-level 0
if "$build/wrencote" check tests/data/nope.wren >/dev/null 2>&1; then
    echo "missing file check should fail" >&2
    exit 1
fi
if "$build/wrencote" run examples/app.wren --listen bad --allow http --no-sandbox >/dev/null 2>&1; then
    echo "bad listen should fail" >&2
    exit 1
fi

"$build/wrencote" run tests/data/cli_app.wren --listen 127.0.0.1:18092 --allow http --no-sandbox &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:18092/ >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -fsS http://127.0.0.1:18092/)" = "cli"
test "$(curl -fsS http://127.0.0.1:18092/stop)" = "bye"
wait "$pid"

# Bracketed IPv6 bind address.
"$build/wrencote" run tests/data/http_app.wren --listen '[::1]:18108' --allow http --no-sandbox >/tmp/wc-ipv6.log 2>&1 &
pid=$!
for _ in $(seq 1 50); do
    if curl -g -fsS 'http://[::1]:18108/health' >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -g -fsS 'http://[::1]:18108/health')" = "ok"
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
