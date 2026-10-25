# Phase 2 evidence — core primitives

**Status:** passed (2026-10-23)
**Command:** `scripts/ci-local.sh` (exit 0)

## Delivered

| File | Purpose |
|---|---|
| `src/core/mem.[ch]` | host allocation wrappers and debug counters |
| `src/core/status.[ch]` | fixed `wc_status` enum, `wc_strerror`, `wc_error` with truncation flag |
| `src/core/arena.[ch]` | bump arena with alignment, reset, high-water mark, checked arithmetic |
| `src/core/buf.[ch]` | owned byte buffer, geometric growth, checked append/appendf, bounds-checked access |
| `src/core/str.[ch]` | borrowed byte view, equality, ASCII case-insensitive compare, prefix/suffix |
| `src/core/log.[ch]` | sink callback, levels, bounded formatting with truncation marker |
| `src/core/clock.[ch]` | injectable monotonic clock (int64 nanoseconds) |
| `src/core/hash.[ch]` | FNV-1a 64 for header lookup |
| `src/core/compiler.h`, `assert.h`, `checked.h` | attributes, debug assert/trap, `ckd_*` fallback |

Tests: `test_arena`, `test_buf`, `test_str`, `test_status`, `test_log`, `test_clock`, `test_hash`,
`test_mem`, `test_alloc_budget`, plus the existing `test_version`.

## Gate results

```
ci-gcc:   build + ctest  -> 10/10 passed
ci-clang: build + ctest  -> 10/10 passed
format-check             -> ok
tidy                     -> ok (diagnostics are warnings only)
symbols                  -> ok (only wc_* exported)
public headers           -> ok
plan references          -> ok
version                  -> ok (0.1.0)
layers                   -> ok (core has no project includes)
docs                     -> ok
bindings                 -> 14 blocks well-formed; native table not present yet
coverage                 -> 91.2% lines (332/364), 100% functions, 82.2% branches
gate selftest            -> all seeded violations rejected
```

## Allocation budget

`test_alloc_budget` proves that a warmed arena and a warmed buffer perform their steady-state work
without host allocation: the allocation counter delta is zero across 100 arena allocations and 100
buffer appends. This is the first enforcement of the request-path allocation invariant.

## Notes

- `stdckdint.h` is absent on GCC 13; `core/checked.h` falls back to `__builtin_*_overflow`, and the
  standard header is used automatically where it exists (Clang 22 here).
- cppcheck and valgrind remain skipped locally (not installed); unchanged from Phase 1.
