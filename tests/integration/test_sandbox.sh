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
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""

# The script top level must itself be sandboxed: outbound HTTP from load-time
# code has to be denied by seccomp, while the same call must succeed without
# the sandbox. Skipped when the optional http module is not compiled in.
if printf 'import "http" for Http\n' >"$root/probe.wren" &&
   "$build/wrencote" check "$root/probe.wren" --allow net >/dev/null 2>&1; then
    python3 -m http.server 18096 --bind 127.0.0.1 >/dev/null 2>&1 &
    probe=$!
    for _ in $(seq 1 30); do
        if curl -fsS http://127.0.0.1:18096/ >/dev/null 2>&1; then break; fi
        sleep 0.1
    done
    if "$build/wrencote" run tests/data/sandbox_toplevel.wren --root "$root" \
        --allow net --no-listen --sandbox >/dev/null 2>&1; then
        echo "sandbox: top-level outbound http escaped" >&2
        kill "$probe" 2>/dev/null || true
        exit 1
    fi
    "$build/wrencote" run tests/data/sandbox_toplevel.wren --root "$root" \
        --allow net --no-listen --no-sandbox >/dev/null 2>&1 ||
        { echo "sandbox: top-level http must work unsandboxed" >&2
          kill "$probe" 2>/dev/null || true
          exit 1; }
    kill "$probe" 2>/dev/null || true
    wait "$probe" 2>/dev/null || true
fi
echo "sandbox: serving, fs root, and escape denial ok"
