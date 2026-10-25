# ADR-0017: HTTP/2 write backpressure and slow-reader isolation

**Status:** Proposed — blocks the Phase 9 gate
**Date:** 2026-10-24

## Context

HTTP/2 conformance is green (h2spec 144 passed, 2 skipped, 0 failed), and h2load serves 100 concurrent
streams at 25.8k req/s. The remaining Phase 9 requirement is slow-reader isolation: a slow consumer on
one stream must not push another stream's p99 beyond 2x the baseline.

The current adapter uses one `wc_h2_server` output buffer and one TLS transport. `nghttp2` provides
per-stream flow control, but all streams share the connection write path. A peer that stops reading can
fill the TLS/socket send buffer, so `conn_h2_flush` returns `EAGAIN` and every stream waits behind that
connection-level backpressure. This is partly inherent to HTTP/2 over one TCP connection; the gate is
about bounded per-stream queues and fair scheduling, not eliminating TCP head-of-line blocking.

## Decision

Do not claim the Phase 9 gate until a slow-reader test and the write-path design are settled. The
proposed design:

1. Keep one connection writer and one nghttp2 session, but add explicit per-stream output accounting
   and a bounded connection queue.
2. Give each stream a maximum buffered response (`max_response_bytes`); if the peer's stream window is
   exhausted, stop producing that stream's DATA and continue serving other streams whose windows allow.
3. Treat `EAGAIN` from the transport as a connection-level pause; when writable, resume the nghttp2
   session rather than dropping or reordering frames.
4. Test with a client that reads slowly but does not stop control-frame processing, measuring another
   stream's p99 against a no-slow-reader baseline. A client that never reads cannot be served on the
   same TCP connection; document that as an inherent limit, not a bug.
5. If fair scheduling requires changing the loop's write path, stop and amend this ADR before adding
   code.

## Consequences

- Phase 9 remains open until the slow-reader test is implemented and measured.
- v0.2.0 must state the observed slow-reader limit and the test method.
- The TLS static-musl packaging question is separate: the current musl release disables TLS/HTTP2, so a
  v0.2 artifact may be glibc/OpenSSL unless a musl OpenSSL build is added.

## Rejected

- Claiming the gate from h2load alone: h2load does not exercise a slow consumer.
- A second listener/connection per stream: defeats HTTP/2 multiplexing and the single-TLS model.
