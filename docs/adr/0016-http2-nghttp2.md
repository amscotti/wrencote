# ADR-0016: Use nghttp2 for HTTP/2 framing, HPACK, and flow control

**Status:** Accepted
**Supersedes:** ADR-0009 (HTTP/2 model)
**Date:** 2026-10-24

## Context

The hand-written path has a frame-header parser and a control-frame connection state machine. The
remaining work — HPACK with a bounded dynamic table and Huffman, CONTINUATION handling, stream state,
and connection/stream flow control — is the highest-risk part of v0.2. nghttp2 1.59 is installed, is
MIT-licensed, and is the reference-quality C implementation of those layers.

## Decision

Use **nghttp2** for HTTP/2 framing, HPACK, stream state, and flow control. Wrencote keeps ownership of:

- TLS and ALPN (`wc_tls_*`, `wc_transport_tls`);
- the Wren handler boundary (`Req`/`Res`, one invocation per request stream);
- request/response size limits, timeouts, logging, and capability policy;
- shutdown and GOAWAY policy.

nghttp2 is linked privately. Its headers do not appear in public headers, and no `nghttp2_*` symbol is
exported from `libwrencote.so`.

The existing `src/http2/frame.c` and `conn.c` control-plane code is retained only as a bounded parser
test surface and will not be the production HTTP/2 path; it is removed once the adapter has equivalent
tests.

## Consequences

- HPACK and flow-control correctness come from a mature implementation instead of new code.
- libnghttp2 becomes a build dependency for HTTP/2-enabled builds; the musl release path may disable it.
- The phase gate still requires h2spec, the 100-stream/slow-reader tests, and an independent fuzz target
  around the adapter and its limits.
- ADR-0009's "no server push, no priority, one handler per stream" behavior remains.

## Rejected

- Hand-written HPACK/framing: highest risk and 8–12 days of work for no product differentiation.
- A separate HTTP/2 proxy: leaves Wrencote unable to serve h2 natively.
