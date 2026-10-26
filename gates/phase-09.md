# Phase 9 — TLS and HTTP/2 (v0.2)

**Status: GATED — 2026-10-24.** v0.2.1 is the HTTP/2 release candidate built from this tree.

## Delivered

- ADR-0008: OpenSSL 3.x is the TLS backend (`a0728c4`).
- `src/tls/tls.[ch]`: OpenSSL server context, certificate/key loading, TLS 1.2/1.3 floor and ceiling,
  no compression/renegotiation, server ALPN selection (`h2`, then `http/1.1`), non-blocking
  `wc_tls_handshake`, `wc_tls_read`, `wc_tls_write`, selected-protocol accessor.
- `src/http2/frame.[ch]`: strict 9-byte frame-header parser with configurable max frame size,
  `WC_EAGAIN` for partial headers, `WC_EPROTO` for oversized frames, reserved stream-id bit ignored.
- `src/http2/server.[ch]`: nghttp2 server-session adapter. It accepts HEADERS/DATA, extracts
  `:method`/`:path`, buffers bounded bodies, submits responses, and uses nghttp2 for HPACK, stream
  state, and flow control. `tests/unit/test_h2_server.c` drives it with a real nghttp2 client
  (GET and POST with a body).
- TLS ALPN routing: `wc_transport_alpn` selects `h2`; the HTTP connection switches to the nghttp2
  session, while HTTP/1.1 clients keep the existing parser. `tests/integration/test_http2_server.sh`
  verifies both protocols on one listener.
- `tests/unit/test_h2_frame.c`: valid/partial/oversized headers and reserved-bit cases.
- `tests/unit/test_tls.c`: real socketpair handshake against an OpenSSL client, ALPN assertion (`h2`),
  encrypted ping/pong, invalid-context and null-argument paths.
- ADR-0015: one `wc_transport` byte interface; HTTP connections use it, TLS is a transport, and HTTP/2
  gets per-stream arenas/buffers. OpenSSL stays out of `src/http`.
- `src/tls/transport.c`: TLS transport over `wc_transport`, lazy non-blocking handshake, partial-write
  handling, close semantics; `tests/unit/test_tls_transport.c` exercises handshake, encrypted I/O,
  early write, closed peer, and failure paths.
- HTTP/1.1 now reads/writes through `wc_transport` instead of raw `recv`/`send`; seccomp allows `writev`.
- `wc_host_serve_tls` and CLI `--tls-cert/--tls-key`; `tests/integration/test_tls_server.sh` serves
  HTTPS HTTP/1.1 and checks ALPN fallback. HTTP/2 advertisement is gated by
  `wc_tls_context_enable_http2` and remains off until the connection layer exists.
- CMake: `wrencote_tls` is built only when OpenSSL is found; `WRENCOTE_BUILD_TLS=OFF` keeps the musl
  release path OpenSSL-free.

## Verification

```
ctest -R test_tls
1/1 Test #22: test_tls ... Passed  0.93 sec
```

`ctest -R 'test_h2_frame|test_h2_server|test_tls|test_tls_transport|test_tls_alpn|test_tls_server|test_http2_server'`:
8/8 passed. Full `ci-local`: **40/40 tests**, all gates green. (`src/http2/conn.[ch]` and its test were
removed in the 0.3.0 pass; the production adapter is covered by `test_h2_server` plus the
integration suite.)

### Conformance status

`h2spec -t -k -p 18462 -P /health` (2.6.0): **146 tests, 144 passed, 2 skipped, 0 failed**.
The full output is committed as `gates/h2spec-2.6.0.txt`. The two skips are optional cases; no required
test fails.

`h2load -n 1000 -c 1 -m 100 -w 20 -W 20` against the same TLS listener:
**1000/1000 requests succeeded**, **25,836 req/s**, 100 concurrent streams. Output committed as
`gates/h2load-1000.txt`.

## Gate evidence

- `h2spec -t -k -p 18120 -P /health`: **146 tests, 145 passed, 1 skipped, 0 failed** against the
  packaged v0.2.1 binary. The skip is the optional TLS renegotiation case.
- `h2load -n 2000 -c 1 -m 100 -w 24 -W 24`: **100% success**, 100 concurrent streams,
  **25,668 req/s**.
- Slow reader: 98-sample p99 **19.9 ms baseline / 20.0 ms slow (1.01x)**, below the 2x gate;
  `test_h2_p99.py` runs inside `test_h2_slow_reader`.
- `test_h2_unique`: 100 streams, 100 unique bodies.
- `fuzz_h2_frame`: **40,854,341 runs in 61 s**, no crash, coverage 20 edges.
- `fuzz_h2_feed`: 3,880,000+ runs in 61 s with a real client HEADERS corpus, no crash. HPACK is
  owned by nghttp2 (a separately fuzzed dependency); Wrencote's adapter path is the fuzz surface here.
- Full `ctest` from the release build: **38/38 passed**.
- Packaged binary: h2 `200 ok` and h1 `200 ok` on one listener.

## v0.2.1 artifact

`dist/wrencote-0.2.1-linux-x86_64.tar.gz` (glibc, dynamic OpenSSL/nghttp2), SHA256
`13ced57b3be4192f891e95f0c22027b97e36bd0aec2ca4d4be8fdc965eb21bcd`. The tarball includes
`docs/http2.md` and an SBOM naming 0.2.1, OpenSSL 3.0.13, and nghttp2 1.59.0.
