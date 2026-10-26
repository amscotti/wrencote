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
- **Update (0.3.0):** the TLS/HTTP2-off configuration now configures *and links* (guards in
  `http/server.c`, `http/response.c`, `server/host.c`), and CI has an `http1-only` job. The musl
  artifact still needs a TLS-free OpenSSL decision for v0.2.x.

### G4. Assertions are nominal

- **Symptom:** `WC_ASSERT` exists but has no production call sites; tests use plain `assert` with
  `-UNDEBUG`.
- **Why:** internal invariants are not enforced at the point of violation; failures give little context.
- **Needed:** add `WC_ASSERT_MSG` and use it in loop/parser/transport state transitions; add a small
  `tests/test.h` harness for new tests.

### G5. Module loader is a development stub

- **Symptom:** one flat `module_path`, `name.wren`, no nested/relative imports, no CLI flag, host sets
  `module_path = NULL`. The loader validates names but uses `fopen`, so symlinks under `module_path`
  are followed.
- **Needed:** rooted project loader (v0.3 design): canonical IDs, relative imports, `WC_CAP_MODULES`,
  openat2/no symlink, limits, cache identity, Landlock read rule.

### G6. Fetch is unbuilt

- **Symptom:** no outbound HTTP client, DNS worker, TLS client mode, `Fetch` module, allowlist, or SSRF
  policy.
- **Needed:** v0.4 design in the plan; never block the event loop on DNS/connect/TLS.

## P2 — quality and tooling

### G18. Handlers can block the event loop

- **Symptom:** `Sys.sleep` (bounded to 10 s), the `fs` module's blocking file I/O, and the
  optional synchronous `Http.request` (10 s default, `Http.setTimeout` up to 300 s) all run on the
  loop thread; one slow call stalls every connection for the duration.
- **Status (0.3.0):** the blast radius is bounded — `Http` defaults to 10 s overall / 5 s
  connect with an explicit opt-up, and `Sys.sleep` caps at 10 s. The real fix is the deferred-
  exchange + `curl_multi` design in ADR-0018 (needs G16); blocking remains a documented latency
  risk, not an unbounded outage.
- **Needed:** implement ADR-0018: refcounted `wc_exchange` with generation checks, callback-style
  `Http`/`Sys.sleep` APIs driven by the loop, handler return-without-response.

### G19. Listener limitations

- **Status (0.3.0):** EMFILE/ENFILE on accept now pauses the listener for 250 ms via a timer
  instead of spinning; `--listen '[::1]:PORT'` (and any IPv6 bind address) is supported with a
  dual-stack socket; the backlog is 511.
- **Remaining:** a single read event can still dispatch an unbounded batch of pipelined requests
  (bounded only by head size and `keepalive_requests`) — a fairness/head-of-line consideration
  rather than a correctness bug.

### G20. `wc_vm_check` uses Wren private API

- `wrenCompileSource` is re-declared manually and the returned closure is left unrooted; the
  compile also runs under the file path as module name, polluting the module namespace. Works
  today; fragile against Wren upgrades.

### G21. The sandbox is installed after the script top level runs — RESOLVED

- **Resolution (0.3.0):** `wc_host_load_source` (public API) splits reading from executing.
  The CLI now reads the entry script before enabling the sandbox and runs its top level under
  Landlock+seccomp; `test_sandbox.sh` proves a top-level outbound `Http.request` is denied while
  the same call succeeds with `--no-sandbox`. `wc_host_load` remains for embedders that do not
  sandbox.

### G22. `fuzz_h2_feed` is coverage-blind — RESOLVED

- **Resolution (0.3.0):** the target now compiles the nghttp2 adapter sources directly (like
  its sibling targets), so libFuzzer drives adapter coverage (458 counters vs 3 before).

### G23. Heap budget is a request gate, not a hard cap

- **Symptom:** a script that retains memory above `vm_heap_bytes` (e.g. an unbounded module-global
  cache) receives 503 for every request after the first collection fails to bring the live heap
  under budget, and cannot run the code that would free it.
- **Status:** documented in ADR-0014; transient over-budget allocations recover after GC. A hard cap
  still requires process-level limits (`RLIMIT_AS`). Revisit with per-request arena-only policy.

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

- **Status:** stale as of 2026-10-25: a GitHub remote (`origin`) exists and `.github/workflows/ci.yml`
  runs on push (the "dormant" comment in the workflow is outdated). Still missing: published
  releases, external reproducibility check, and an arm64 CI leg (the suite is green on
  Linux aarch64 locally via the dev container).


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
  aborts the stream. Idle timer resets on h2 activity (verified by `test_h2_idle`; the earlier
  "resets on activity" claim here predated the code and was corrected in the 0.3.0 pass).
  Per-stream header byte budget added. Remaining: explicit request deadline test.
  100-unique-body is covered by `test_h2_unique`; `fuzz_h2_feed` exercises the adapter under
  ASan/UBSan with a preface seed.

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
- **Status (0.3.0):** the `wc_mem_malloc`/`free` allocator mixing found by review is fixed
  across http2/response/module; loadable module names are validated as single path segments with a
  1 MiB size cap, and short reads are rejected. The loader still uses `fopen`, so a symlink under
  `module_path` is followed — symlink escape remains part of G5. The rooted loader, explicit
  unknown-module denial, and `test` gating remain open.

## Current verified baseline

- 40/40 ctest targets on Linux aarch64 (Debian 13, gcc 14, ASan+UBSan dev preset, dev container),
  including regressions for pipelining, h1 backpressure (real EAGAIN), h2 idle survival, h2 HEAD
  no-body, loop token wrap, timer-heap double insert, heap-budget recovery, top-level sandbox
  denial, and IPv6 binding.
- Gates re-run on aarch64 (ASan build): h2load 1000/1000 succeeded, 0 failed (58.6k req/s);
  slow-reader p99 6.6 ms baseline / 6.5 ms with a concurrent slow stream (2x gate);
  h2spec 2.6.0: 146 passed, 0 skipped, 0 failed.
- Fuzzing: fuzz_http_request, fuzz_h2_frame, fuzz_h2_feed coverage-driven (~90M total execs
  across sessions, no findings).
- TLS/HTTP2-off configuration builds and links (CI `http1-only` job); CI recipe validated
  dependency-install + configure + build on ubuntu-24.04 (gcc-13 and clang-18 legs).
- Known toolchain limits: gcc <= 13 + ASan on aarch64 hangs in Wren string concat (CMake warns);
  TSan needs a real Linux host (`setarch -R`), not unprivileged Docker.
- Earlier verified on x86-64: v0.1.0 static musl HTTP/1.1; v0.2.0 glibc TLS/HTTP2.
