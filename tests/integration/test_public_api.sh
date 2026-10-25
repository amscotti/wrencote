#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/release}"
prefix="$(mktemp -d)"
pid=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$prefix"
}
trap cleanup EXIT

cmake --install "$build" --prefix "$prefix" >/dev/null
cc examples/embed.c -I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" -lwrencote -lm -o "$prefix/embed"
"$prefix/embed" examples/app.wren >"$prefix/embed.log" 2>&1 &
pid=$!
for _ in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:18090/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test "$(curl -fsS http://127.0.0.1:18090/health)" = "ok"
test "$(curl -fsS http://127.0.0.1:18090/json)" = '{"ok":true}'
test "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:18090/nope)" = "404"
if "$prefix/bin/wrencote" check tests/data/bad.wren >"$prefix/check.log" 2>&1; then
    echo "check should have failed" >&2
    exit 1
fi
grep -q "bad.wren" "$prefix/check.log"
echo "public API: install, embed, serve, check ok"
