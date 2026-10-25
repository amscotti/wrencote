# Wrencote — open gaps and issues

This is the working tracker for gaps that are not closed by the current gates. It is intentionally
honest: an item here is not “done” because it has a plan or a test that only partially exercises it.

## P0 — blocks a phase gate

### G1. HTTP/2 slow-reader p99

- **Symptom:** baseline health p99 6.45 ms; with a concurrent 1 MiB stream read slowly, p99 26.58 ms
  (4.1x). The gate is 2x. Priority-only reached 26.6 ms; deferred-large submission reached 42.4 ms and
  was reverted.
- **Why:** one connection-wide nghttp2/TLS output path; a large response can occupy queued TLS output
  ahead of a small response.
- **Evidence:** `gates/phase-09.md`, `tests/integration/test_h2_p99.py`, `gates/h2load-1000.txt`.
- **Needed:** bounded per-stream quanta and output high-water marks; a test that measures p99, not a
  single request.
- **Status:** p99 gate passes with the flush-before-last-ready-handler boundary (98-sample run
  22.5 ms vs 23.8 ms). Remaining work is bounded quanta/output cap (M2–M3), not the gate.

### G2. Phase 8 Fil-C oracle

- **Symptom:** Fil-C is unavailable on the development host; the independent memory-safety oracle was
  never run.
- **Why:** Phase 8’s DoD names Fil-C as a gate; ASan/UBSan/TSan do not replace it.
- **Needed:** install a Fil-C toolchain or record an explicit, reviewed exception with equivalent
  evidence. Do not silently mark Phase 8 fully complete.

## P1 — release/architecture gaps

### G3. v0.2 artifact is glibc-only for TLS/HTTP2

- **Symptom:** the musl path disables TLS and HTTP/2; the v0.2 artifact dynamically links OpenSSL and
  nghttp2.
- **Why:** static musl OpenSSL has not been built; this is a packaging decision, not a code bug.
- **Needed:** either build OpenSSL with musl and package a static TLS artifact, or keep the glibc
  artifact as the documented v0.2 distribution and label the static musl artifact HTTP/1.1-only.
- **Status:** decided: glibc v0.2 for now; revisit for a static v0.2.x.

### G4. Assertions are nominal

- **Symptom:** `WC_ASSERT` exists but has no production call sites; tests use plain `assert` with
  `-UNDEBUG`.
- **Why:** internal invariants are not enforced at the point of violation; failures give little context.
- **Needed:** add `WC_ASSERT_MSG` and use it in loop/parser/transport state transitions; add a small
  `tests/test.h` harness for new tests.

### G5. Module loader is a development stub

- **Symptom:** one flat `module_path`, `name.wren`, no nested/relative imports, no CLI flag, host sets
  `module_path = NULL`.
- **Needed:** rooted project loader (v0.3 design): canonical IDs, relative imports, `WC_CAP_MODULES`,
  openat2/no symlink, limits, cache identity, Landlock read rule.

### G6. Fetch is unbuilt

- **Symptom:** no outbound HTTP client, DNS worker, TLS client mode, `Fetch` module, allowlist, or SSRF
  policy.
- **Needed:** v0.4 design in the plan; never block the event loop on DNS/connect/TLS.

## P2 — quality and tooling

### G7. HTTP/2 adapter test coverage

- The nghttp2 adapter has unit tests and h2spec, but error paths (stream close during send, data
  provider errors, window exhaustion) are not all covered. The p99 script is not in CI because it fails.

### G8. Layer/documentation drift

- `tools/check_layers.py` allows `http -> http2` to bridge the h2 handler; the plan’s layer table does
  not describe that edge. ADR-0009 is superseded by ADR-0016 but remains in the ADR set intentionally.
- `docs/http2.md` is missing; `BINDINGS.md` does not yet describe an h2-visible API beyond
  `Req.httpVersion`.

### G9. CI dependencies

- TLS/h2 tests require OpenSSL, nghttp2, `h2spec`, `h2load`, and Python `h2`. h2spec/h2load are not
  vendored; Python `h2` is installed in user site only. Document prerequisites or provide a container.

### G10. No remote CI/artifact hosting

- Repository remains local-first; no GitHub remote, no published release, no external reproducibility
  check.


### G11. HTTP/2 request/response metadata parity

- **Symptom:** headers and query strings are dropped; `/inspect?a=1&b=2` 404s and `/headers` 500s over h2;
  response headers are discarded and content type is hardcoded.
- **Status:** fixed for request headers/query and response headers/content type; `/inspect` and
  `/headers` parity plus `x-test`/custom content-type are in `test_http2_server.sh`. Keep G11 open only
  until the full h1/h2 parity matrix and limits land.
- **Needed:** extend parity tests to all h1 fixtures and error paths.

### G12. HTTP/2 limits and timers absent

- **Symptom:** h2 ignores head/body/response caps, header count/list limits, idle/request timeouts,
  SETTINGS; 100 streams can hold 100 × 1 MiB and arbitrarily long `:path` strings.
- **Status:** body/response/path limits and SETTINGS max-concurrent/header-list are wired; path oversize
  aborts the stream. Remaining: per-stream idle/request deadlines and REFUSED_STREAM behavior.
- **Status:** idle timer now resets on h2 activity; excess streams get `REFUSED_STREAM`. Remaining:
  explicit request deadline test. 100-unique-body is covered by `test_h2_unique`; `fuzz_h2_feed` exercises
  the adapter under ASan/UBSan with a preface seed.

### G13. Release integrity is broken — RESOLVED

- **Symptom:** v0.2.0 tagged while phase-09 says not gated; SBOM is a stale 0.1.0 copy missing OpenSSL
  and nghttp2; release script runs no tests and can label a TLS-free musl build 0.2.0; tarball omits
  docs and `docs/http2.md` is missing.
- **Needed:** SBOM from real dependency versions; release gate checks clean tree/tests/tag/features;
  ship docs; correct README/CHANGELOG.

### G14. Gate scripts that cannot fail

- **Symptom:** `check_bindings.py` never parses `bindings.c`; `check_layers.py` lacks cycle detection;
  no allocation gate despite I3; valgrind/cppcheck silently skipped; install/embedding test manual.
- **Needed:** make gates fail on seeded violations; run one `gates` target in CI; activate or remove
  dormant CI.

### G15. Config ABI cannot add modules/Fetch fields

- **Symptom:** `wc_host_new` rejects `struct_size < sizeof *config`; adding `module_root`/Fetch fields
  breaks existing callers despite the plan calling it ABI-compatible.
- **Needed:** per-field `struct_size` validation or a versioned config; add a link/run test with an
  older-size config.

### G16. Fetch cannot answer a handler

- **Symptom:** `invoke_handler` sends 500 if the handler returns without a response; `Req`/`Res` hold raw
  pointers that die at return. Fetch completion is too late.
- **Needed:** refcounted `wc_exchange` with generation/validity checks and deferred-response state;
  `wc_loop_post` for resolver completions; sandbox policy for DNS/CA.

### G17. Module resolver ownership/capability closure

- **Symptom:** Wren frees a non-input resolved name with `wc_vm_realloc`; a canonical ID allocated with
  `malloc` corrupts the heap. Returning NULL lets Wren fall back to `meta`/`random`; `test` is always
  importable.
- **Needed:** allocate canonical IDs with the VM allocator; deny unknown modules explicitly; ASan
  10k-resolve test; remove the flat `module_path` stub.

## Current verified baseline

- 37/37 tests, 85.3% line coverage.
- h2spec 144 passed, 2 skipped, 0 failed.
- h2load 1000/1000, 100 concurrent streams, 25,836 req/s.
- v0.1.0 static musl HTTP/1.1; v0.2.0 glibc TLS/HTTP2.
