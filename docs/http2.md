# HTTP/2

HTTP/2 is negotiated with ALPN (`h2`) on the same TLS listener as HTTP/1.1. The
engine is nghttp2; Wrencote keeps ownership of TLS, the `Req`/`Res` boundary,
limits, logging, capabilities, and shutdown.

## Configuration

```sh
wrencote run app.wren --listen 0.0.0.0:8443 \
    --tls-cert cert.pem --tls-key key.pem --allow http
```

An HTTP/1.1 client on the same listener falls back automatically. Without
`--tls-cert`/`--tls-key` the listener is HTTP/1.1 only; cleartext h2c is not
implemented.

## Limits

Limits are shared with HTTP/1.1 (`src/http/limits.h`) and copied into the
nghttp2 adapter when the connection switches protocols:

- request path/header budget: 8 KiB and 32 KiB header list
- request body: 1 MiB
- response body: 1 MiB
- concurrent streams: 100 (`MAX_CONCURRENT_STREAMS`)
- idle timeout: 5 s, reset on connection activity
- excess streams receive `REFUSED_STREAM`

Oversized bodies are rejected while DATA frames are being accumulated; an
oversized path aborts only that stream.

## Wren surface

Handlers see the same `Req` and `Res` objects as HTTP/1.1. `req.httpVersion` is
`"2"`, `req.path` excludes the query, `req.query` is parsed from `:path`, and
regular request headers (including `host`) are exposed through `req.header()`.
`res.header()` and `res.send()` map to HEADERS and DATA; content type defaults
to `text/plain; charset=utf-8`.

## Write path

nghttp2 serializes frames. After each batch of ready handlers the server flushes
once before the last ready handler, which keeps a small response ahead of a
large one while avoiding an `SSL_write` per request. The output buffer is
drained by the event loop on write readiness (`WC_EV_WRITE`); a failed flush
pauses dispatch until the connection is writable.

## Verification

```sh
h2spec -t -k -p 18462 -P /health
h2load -n 2000 -c 1 -m 100 -w 24 -W 24 https://localhost:8443/health
python3 tests/integration/test_h2_unique.py 18462
```

`tests/integration/test_http2_server.sh` covers ALPN, HTTP/1.1 fallback, query
and header parity, and response headers. `test_h2_p99.py` measures the
slow-reader gate. `tests/fuzz/fuzz_h2_feed.c` fuzzes the adapter under
ASan/UBSan.

Last full run (2026-10-26, Linux aarch64, ASan build): h2spec 2.6.0
**146 passed, 0 skipped, 0 failed**; h2load 1000/1000 succeeded (58.6k req/s);
slow-reader p99 6.6 ms baseline vs 6.5 ms with a concurrent slow stream.
