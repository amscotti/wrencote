#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build="${1:-build/dev}"
py_pid=""
cleanup() { if [ -n "$py_pid" ]; then kill "$py_pid" 2>/dev/null || true; wait "$py_pid" 2>/dev/null || true; fi; }
trap cleanup EXIT
python3 - <<'PY' &
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
echo "http client: ok"
