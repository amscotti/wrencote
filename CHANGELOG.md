# Changelog

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

# Changelog

## [0.2.1] - 2026-10-24

- HTTP/2 request/response metadata parity with HTTP/1.1.
- h2 body/response/path limits, SETTINGS, idle timeout, and REFUSED_STREAM.
- Slow-reader p99 gate met by the flush boundary; 100 unique bodies and h2 fuzz targets added.
- Release pipeline runs tests and ships versioned SBOM plus docs.

## 0.1.0 — 2026-10-23

- Embeddable Wren host with epoll loop, HTTP/1.1, keep-alive, and bounded parsing.
- Capability modules: `net`, `fs` (root-scoped), `sys`, plus pure-Wren `json` and `router`.
- CLI `run`/`check`, installed CMake package, pkg-config file, 38-line embedding example.
- Landlock filesystem scope and seccomp syscall allowlist applied by the CLI.
- Parser fuzzing, 1 MiB body/response limits, sandbox tests, soak and latency gates.
