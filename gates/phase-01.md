# Phase 1 evidence — foundation

**Status:** passed with recorded deviations (2026-10-23)
**Command:** `scripts/ci-local.sh` (exit 0)

## Toolchain actually used

| Tool | Version | Note |
|---|---|---|
| CMake | 4.4.4 | installed under `~/.local` via pip |
| GCC | 13.3.0 | bootstrap only; compiles `-std=c2x` because GCC 13 predates the `c23` spelling |
| Clang | 22.1.8 | via Zig 0.17.0 `zig cc` (satisfies the Clang 19+ floor) |
| clang-format | 21.1.8 | pinned formatter |
| clang-tidy | 21.1.6 | pinned linter |
| gcovr | 8.7 | coverage |
| cppcheck / valgrind | absent | require root; reported as skipped by the gate |

Release floor remains GCC 15+/Clang 19+ (`docs/toolchain.md`). The GCC 13 job is a development
allowance, not a supported build.

## Gate results

```
ci-gcc:   configure + build + ctest  -> 1/1 tests passed
          format-check               -> ok
          tidy                       -> ok (no diagnostics; 756 third-party warnings suppressed)
          symbols                    -> ok (only wc_* exported)
ci-clang: configure + build + ctest  -> 1/1 tests passed
          format-check               -> ok
          tidy                       -> ok
          symbols                    -> ok
public headers                        -> ok
plan references                       -> ok
version consistency                   -> ok (0.1.0)
layers                                -> ok
docs                                  -> ok
bindings                              -> 14 blocks well-formed; native table not present yet
coverage                              -> src/core/version.c 100% lines (2/2), gate >= 85%
gate selftest                         -> all seeded violations rejected
```

`cppcheck` and `valgrind` were skipped because they are not installed on this host. They remain
required by the release gate and are wired into `scripts/ci-local.sh` for the pinned image.

## Seeded-failure proof

`scripts/gate-selftest.sh` verifies that the gates are not vacuous:

- a shared library exporting `not_wc` is rejected by `check_symbols.sh`;
- `src/core` including `http/parser.h` is rejected by `check_layers.py`;
- an undocumented `WC_API` declaration is rejected by `check_docs.py`;
- a code comment containing a phase number is rejected by `check_no_plan_refs.sh`.

## Deliverables present

CMake 4 project with `dev`/`release`/`ci-gcc`/`ci-clang`/`coverage`/`tsan` presets; static and shared
libraries with a linker version script; one unit test; clang-format/clang-tidy configuration; gate
scripts; `scripts/ci-local.sh`; ADRs 0001–0007 and 0011; `docs/architecture.md`; `docs/toolchain.md`;
`gates/thresholds.toml`; `BINDINGS.md`; dormant GitHub workflow.

## Deviations and follow-ups

1. GCC 15/Clang 19 release image is not available without root; GCC 13 is used for the second local
   compiler. Phase 4 must either use the Zig Clang for `#embed` or keep the byte-array fallback.
2. `cppcheck` and `valgrind` are not installed locally; both are skipped, not passed.
3. No GitHub remote by design; publication is plan §11.
