# Changelog

## 0.3.0 — 2026-10-26

Correctness and hardening pass over the event loop, HTTP/1.1, HTTP/2, TLS, and sandbox layers.
Full test suite (40 ctest targets) green on Linux aarch64 (Debian 13, gcc 14, ASan+UBSan).

- Event loop: http layer now uses the loop's `WC_EV_*` event namespace instead of raw
  `EPOLL*` constants. Previously write interest was silently dropped (responses larger than the
  socket buffer stalled after the first EAGAIN) and error events were misrouted into the write
  path.
- Event loop: poll tokens now carry a per-slot generation, and dispatch compares the full stored
  token, so events for a registration removed earlier in the same batch cannot be delivered to a
  reused slot — including after the generation wraps. Regression test cycles 2^20 registrations.
- HTTP/1.1: pipelined requests are answered without waiting for another read event (previously
  they stalled into a 408); the regression test uses a buffered reader because pipelined responses
  commonly coalesce in one segment.
- HTTP/1.1: `Host` is required on HTTP/1.1 requests (400 otherwise); duplicate and empty `Host`
  fields are rejected; the version token is matched case-sensitively.
- HTTP/1.1: the final chunked boundary (`stream_end`) closes the connection once the last chunk
  drains, or re-arms a bounded drain deadline. Connection teardown is fully deferred to the reap
  step, so a handler that calls `sseClose()` can still read `req`; handlers that close the
  connection no longer race the dispatch flush.
- HTTP/1.1: a 431 can no longer clobber a response that is still flushing; accept-path fd
  ownership is single-owner; a response that stalls on EAGAIN with no other deadline gets a 60 s
  write timeout; a timer expiry never injects a 408 into an already-started response; SSE streams
  extend their deadline on every successful write instead of dying at a fixed 120 s.
- HTTP/2: the idle deadline resets on h2 read/write activity (previously every h2 connection was
  force-closed five seconds after accept); regression test added.
- HTTP/2: stream frees during the ready-stream dispatch loop are deferred with a separate dead-list
  link, so a closed successor cannot truncate the live ready-stream walk.
- HTTP/2: `feed`'s `consumed` result is honored by the connection layer (pending bytes survive
  EAGAIN and a full 16 KiB buffer is fed, not zero-read); per-stream header byte budget added; the
  101st header is rejected with `ENHANCE_YOUR_CALM` instead of being dropped; a failed response
  submit resets the staged body and resets the stream.
- HTTP/2: `HEAD`/204/304/1xx responses carry no DATA frames (h1 parity); the artificial 16-byte
  `:method` cap is removed; removed the nonstandard per-response PRIORITY frame and the dead
  hand-rolled frame parser (`src/http2/conn.c`) that rejected HEADERS/DATA.
- TLS: `SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER` (prevents OpenSSL
  `BAD_WRITE_RETRY` kills under h2 backpressure), `SSL_MODE_RELEASE_BUFFERS`, and the TLS 1.2
  session-ID cache disabled. A short `SSL_write` now reports a short `writev` instead of dropping
  the rest of an iovec. ALPN keeps the RFC 7301 fatal alert when there is no protocol overlap.
- TLS: handshake progress tracks what OpenSSL is actually blocked on (`WC_EV_READ`/`WC_EV_WRITE`)
  and write readiness retries the handshake; previously every handshake requested writability and
  a one-byte partial ClientHello spun a core at 100% without making progress.
- Modules: the loop-blocking calls are bounded — `Http` defaults to a 10 s overall / 5 s connect
  timeout with a new `Http.setTimeout(ms)` (50..300000) opt-up, and `Sys.sleep` caps at 10 s. A
  stalled request aborts the fiber with a timeout error (integration test added). Full async is
  designed in ADR-0018 (deferred exchanges + curl_multi on the loop; GAPS G18/G16).
- Sandbox: allowlist gains `clock_nanosleep`/`nanosleep` (Sys.sleep under the sandbox was a silent
  no-op) and `getsockopt`; the arch check admits only the build architecture (fail-closed
  elsewhere). The CLI reads the entry script *before* enabling the sandbox and runs its top level
  sandboxed via the new public `wc_host_load_source` API (GAPS G21); a top-level outbound
  `Http.request` is now denied under `--sandbox`.
- HTTP listener: `--listen '[::1]:PORT'` and any IPv6 bind address are supported (dual-stack
  socket); accept exhaustion (EMFILE/ENFILE) pauses the listener for 250 ms instead of spinning;
  the backlog is 511.
- HTTP/1.1 conformance: 204 and 1xx responses no longer carry `Content-Length` (RFC 9110 §8.6);
  SSE streams advertise `Connection: close`, which is what actually happens; the heap-budget 503
  carries `Retry-After: 1`.
- VM: the heap budget is a live predicate (`heap_used > heap_limit`), the host forces a collection
  and then answers 503 while it stays exceeded, and a budget below the VM baseline is rejected at
  startup; frees now clear the predicate. Module loading rejects short reads and validates path
  segments; `Server.serve` guards missing module variables instead of dereferencing NULL.
- Loop API: `wc_loop_defer` wakes before queuing (no task is left queued when it reports failure);
  `wc_loop_watch_signal` validates signal numbers and rolls back both new and update-failed
  watches; timer heap rejects double insertion; `wc_poll_event.fd` renamed to `token` (it was
  never an fd).
- Build: TLS/HTTP2-off configuration links again (compile guards) and CI has an `http1-only` job;
  CI installs CMake 4.4, OpenSSL/nghttp2/libcurl dev packages, and Python `h2`; clang-format/clang-tidy are
  optional at configure time; the `ci-clang` preset uses `clang` instead of the nonexistent
  `zigcc`. Fuzzer builds require Clang; `fuzz_h2_feed` compiles the adapter directly so it is
  coverage-driven (458 counters, previously 3). The runtime image links libcurl so the `http`
  module and `--allow http` are real.

## 0.2.1 — 2026-10-24

- HTTP/2 request/response metadata parity; h2 limits, SETTINGS, idle timeout, REFUSED_STREAM.
- Slow-reader p99 gate and 100 unique bodies; h2 fuzz targets.

## 0.2.0 — 2026-10-24

- HTTP/2 over TLS via nghttp2: ALPN `h2`, multiplexed streams, HPACK/flow control delegated to the
  engine, per-request Wren handler bridge.
- TLS transport with HTTP/1.1 fallback; `--tls-cert`/`--tls-key`.
- h2spec 2.6.0: 144 passed, 2 skipped, 0 failed.
- 100 concurrent streams; 25,836 req/s h2load under a 20-bit window.
- Slow-reader isolation test: a 1 MiB stream read slowly did not stall `/health`.
- Known limit: a peer that stops reading entirely can still head-of-line block the shared TLS
  connection; see ADR-0017.
- v0.2 artifact is a glibc build with dynamic OpenSSL/libnghttp2; the static musl release remains
  HTTP/1.1-only.

## 0.1.0 — 2026-10-23

- Embeddable Wren host with epoll loop, HTTP/1.1, keep-alive, and bounded parsing.
- Capability modules: `net`, `fs` (root-scoped), `sys`, plus pure-Wren `json` and `router`.
- CLI `run`/`check`, installed CMake package, pkg-config file, 38-line embedding example.
- Landlock filesystem scope and seccomp syscall allowlist applied by the CLI.
- Parser fuzzing, 1 MiB body/response limits, sandbox tests, soak and latency gates.
