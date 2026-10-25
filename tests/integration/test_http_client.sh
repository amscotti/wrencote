#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
py_pid=""
stall_pid=""
cleanup() {
    for pid in "$py_pid" "$stall_pid"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
}
trap cleanup EXIT
python3 - >/dev/null 2>&1 <<'PY' &
import http.server
import socketserver
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"hello" if self.path == "/full" else b"event: progress\ndata: 42\n\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *args):
        pass
class Server(socketserver.TCPServer):
    allow_reuse_address = True
with Server(("127.0.0.1", 18311), Handler) as server:
    server.serve_forever()
PY
py_pid=$!
for _ in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:18311/full >/dev/null 2>&1; then break; fi
    sleep 0.1
done
"$build/wrencote" run tests/data/http_client.wren --allow net,sys --no-listen --no-sandbox

# A stalled origin must abort within the configured timeout (bounds the loop
# stall; see GAPS G18 and ADR-0018 for the async design).
python3 - >/dev/null 2>&1 <<'PY' &
import socket
server = socket.socket()
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", 18312))
server.listen(8)
while True:
    try:
        conn, _ = server.accept()  # accept, then never answer
    except OSError:
        break
PY
stall_pid=$!
cat >/tmp/wc-http-timeout.wren <<'WREN'
import "http" for Http
Http.setTimeout(500)
var error = Fiber.new { Http.request("GET", "http://127.0.0.1:18312/", "", null) }.try()
if (error == null) Fiber.abort("stalled request should time out")
if (!error.contains("Timeout")) Fiber.abort("unexpected error: " + error)
System.print("timeout error ok")
WREN
start=$(date +%s)
"$build/wrencote" run /tmp/wc-http-timeout.wren --allow net --no-listen --no-sandbox
elapsed=$(( $(date +%s) - start ))
if [ "$elapsed" -gt 10 ]; then
    echo "http client: timeout took ${elapsed}s" >&2
    exit 1
fi
echo "http client: ok"
