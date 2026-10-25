# Phase 7 gate — embedding API, CLI, install surface, examples

**Date:** 2026-10-23
**Commit:** `b902aa3`
**Tests:** 26 total (25 prior + `test_cli`)

## Delivered

| Area | Files | What it does |
|---|---|---|
| Public API | `include/wrencote/wrencote.h` | `wc_host_check`, `wc_host_last_error`, `wc_host_set_args`; lifecycle and config documented |
| CLI | `src/cli/main.c` | `run`, `check`, `--listen`, `--allow`, `--root`, `--max-request`, `--vm-heap`, `--log-level`, `--version`, `--help`; exit 0/1/2 |
| Examples | `examples/embed.c`, `examples/app.wren` | 38-line embedder; `/`, `/health`, `/json` routes |
| Install | `CMakeLists.txt`, `cmake/wrencoteConfig.cmake.in`, `cmake/wrencote.pc.in` | headers, static/shared lib, CMake package, pkg-config, CLI |
| Docs | `docs/embedding.md`, `docs/cli.md` | install, link, lifecycle, errors, CLI options |
| Tests | `tests/integration/test_cli.sh`, `test_public_api.sh`, `tests/data/args_app.wren` | CLI behaviour, installed-prefix build, `Sys.args` |

The public surface remains host-only and opaque: no loop, HTTP, or VM internals are installed. The
plan's earlier mention of public `http.h`/`modules.h`/`loop.h` was deliberately narrowed during the
API freeze; exposing those would leak the internals the review gate forbids.

`wc_host_check` compiles without executing top-level code by calling Wren's `wrenCompileSource`
through the internal VM layer. `wrencote check tests/data/bad.wren` prints `bad.wren:2` and exits 1.
`--max-request` now reaches the parser limit, not just the response cap.

## Verification

```
ci-gcc      26/26 tests
ci-clang    26/26 tests
tsan        26/26 tests (setarch -R)
coverage    85.0% lines, 99.1% functions, 69.3% branches (gate >=85%)
format, clang-tidy, symbols, public headers, layers, docs, bindings,
plan-refs, version, gate selftest: pass
```

Install and consumer checks:

```
cmake --install build/release --prefix /tmp/wrencote-prefix
  bin/wrencote
  include/wrencote/{wrencote,status,modules,clock,version,export}.h
  lib/libwrencote.a
  lib/libwrencote.so.0.1.0 (+ symlinks)
  lib/cmake/wrencote/{wrencoteConfig,wrencoteConfigVersion,wrencoteTargets}.cmake
  lib/pkgconfig/wrencote.pc

tests/integration/test_public_api.sh build/release
  public API: install, embed, serve, check ok
```

The public-API test installs to a temp prefix, compiles `examples/embed.c` with
`-I$prefix/include -L$prefix/lib -lwrencote` only, serves `/health` and `/json`, returns 404 for an
unknown path, and confirms `check bad.wren` exits non-zero. A separate CMake consumer using
`find_package(wrencote CONFIG REQUIRED)` and `Wrencote::wrencote_static` configures and builds.

`wrencote run examples/app.wren --listen 127.0.0.1:8080 --allow http,json` was exercised with curl:

```
/health -> 200 "ok\n"
/json   -> 200 {"ok":true}
/nope   -> 404
```

`examples/embed.c` is 38 lines and uses only `wrencote/wrencote.h`.

## Deviations and notes

1. **Public headers narrowed to the host API.** `http`, `loop`, and `modules` remain private; the
   install ships only opaque handles and the documented host surface.
2. **`wc_host_check` is a new public entry point.** Wren has no public compile-only API; the VM layer
   calls the vendored `wrenCompileSource` and does not execute top-level code.
3. **`--max-request` caps both request body and response body.** The parser limit is now plumbed from
   `max_body_bytes`; the 1 MiB hard ceiling remains.
4. **`Sys.args` is populated by `wc_host_set_args`.** CLI arguments after `--` reach the script; a
   library host may pass none.
5. **Example port 18090.** `examples/embed.c` uses a fixed port to keep the public-API test
   deterministic; the CLI and docs use 8080.
