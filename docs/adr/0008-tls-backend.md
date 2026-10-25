# ADR-0008: OpenSSL is the TLS backend

**Status:** Accepted (Phase 9 start)
**Date:** 2026-10-23

## Context

Phase 9 needs TLS 1.2/1.3, certificate/key loading, ALPN (`h2`, `http/1.1`), no renegotiation, no
compression, and a non-blocking handshake driven by the existing epoll loop. The two candidates were
mbedTLS (small footprint, permissive licence) and OpenSSL (ubiquitous, mature TLS 1.3 and ALPN).

## Decision

Use **OpenSSL 3.x** (`libssl-dev`), linked privately by the TLS layer. It is already present on the
target Linux distributions, exposes `SSL_CTX_set_alpn_select_cb`, supports TLS 1.2/1.3 and
`SSL_CTX_set_min_proto_version`, and has the certificate-loading and error APIs the host needs.
mbedTLS remains the fallback if binary size becomes the dominant constraint; its smaller footprint
does not offset the extra integration and test surface for v0.2.

The TLS layer will expose only a `wc_tls_*` interface. OpenSSL headers will not appear in public
headers and `libwrencote` will not expose OpenSSL symbols.

## Consequences

- Binary size and CVE surface grow; the release SBOM must list OpenSSL.
- The static musl artifact needs an OpenSSL static build; until that exists, the TLS-enabled binary
  is a glibc build, and the musl artifact remains HTTP/1.1-only.
- TLS is a v0.2 feature; the v0.1.0 tag remains HTTP/1.1-only as documented.

## Rejected

- **mbedTLS**: smaller, but a second TLS implementation to package and patch; OpenSSL is already the
  system TLS provider.
- **wolfSSL**: good size, but no existing dependency in the build environment.
- **Custom TLS**: rejected outright.
