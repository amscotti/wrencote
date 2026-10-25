# ADR-0015: One transport interface, per-stream buffers for HTTP/2

**Status:** Accepted
**Date:** 2026-10-23

## Context

HTTP/1.1 currently owns a raw `fd` and calls `recv`/`send` directly, while `wc_transport` exists but has no
consumers. The TLS wrapper is also fd-based and separate. HTTP/2 needs TLS, one connection-level writer,
many stream-level buffers, and per-stream arenas; adding those beside the current path would create a
second server implementation and make HTTP/1.1 regressions likely.

## Decision

`wc_transport` is the only byte interface used by HTTP connection code. A plain transport wraps sockets;
a TLS transport wraps `wc_tls_conn` and owns the non-blocking handshake. The HTTP server accepts a
transport factory, so it never includes OpenSSL or TLS headers. HTTP/1.1 keeps one connection arena and one
output buffer. HTTP/2 will add one arena and one response buffer per stream, with the connection owning
the single transport write queue and flow-control scheduler.

TLS is constructed in the server/host layer and passed to the HTTP server as an opaque factory. ALPN is
selected by the TLS transport; h2 and HTTP/1.1 share the listener and the HTTP/2 connection type branches
on the selected protocol.

## Consequences

- HTTP/1.1 moves from `recv`/`send` to `wc_transport_read`/`wc_transport_writev`; the Phase 5 suite is the
  regression gate.
- Partial writes and `EAGAIN` are defined by the transport, not by each protocol.
- HTTP/2 stream buffers are required; a connection-wide output buffer is not sufficient.
- OpenSSL remains absent from `src/http` and public headers.

## Rejected

- Separate TLS/h2 listener: duplicates accept, limits, timers, and shutdown.
- Raw `fd` plus `SSL*` branches inside HTTP/1.1: every read/write site would need TLS conditionals.
