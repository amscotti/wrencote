# Phase 4 gate — VM embedding, module loader, capabilities

**Date:** 2026-10-23
**Commit:** `3c56007`
**Wren pin:** `99d2f0b8fc2686134b32b18166e037639f7e9f2c` (MIT), vendored under `third_party/wren/`
**Tests:** 20 total (18 prior + `test_vm`, `test_host`)

## Delivered

| Area | Files | What it does |
|---|---|---|
| Public surface | `include/wrencote/wrencote.h`, `status.h`, `clock.h`, `modules.h` | Opaque `wc_host`, config with defaults, capability bits, stable status enum, log/clock hooks |
| Host lifecycle | `src/server/host.c` | `wc_host_new/load/run/stop/free`, config validation, loop + VM ownership |
| VM | `src/vm/vm.c`, `vm.h` | Wren callbacks, enter/exit depth guard, status mapping, handles, error text |
| Memory | `src/vm/memory.c` | Counting `reallocateFn`, peak tracking, advisory budget |
| Modules | `src/vm/module.c` | Capability-gated resolver: native table, dev-only disk path, deny-by-default |
| Bindings | `src/vm/bindings.c` | Static `(module, class, signature, static)` table, arity check, foreign class allocation |

The Wren tree is compiled as a separate object target with `-w`; project warning flags are unchanged.

## Verified behaviour

- `hello.wren` loads and runs; `System.print` reaches the host log sink (`test_host` counts it).
- `import "fs"` with no `WC_CAP_FS` returns `WC_EPERM`; the same import succeeds with the bit set.
- Unknown modules are denied; dev-only disk modules load through `tests/data/mods/helper.wren`.
- `Echo.ping(41)` returns 42, `Echo.new(7).value` returns 7 — foreign static method, getter, and allocation.
- Calling `wc_vm_call` from inside a foreign method returns `WC_EBUSY`; the runtime error says `re-entry blocked`.
- A 128 KiB budget is exceeded by a 50,000-element list without corrupting the VM; `over_budget` is set and the script completes. This is the ADR-0014 correction: Wren cannot survive allocator failure.
- 10,000 handle make/release cycles leave the handle counter at 1 (the VM-owned noop handle), then 0 after free.
- Invalid configs (`struct_size`, `tls`, `WC_CAP_FS` without `fs_root`) return `WC_EINVAL`; missing script returns `WC_ENOENT`.

## Gate results

```
ci-gcc      20/20 tests
ci-clang    20/20 tests
tsan        20/20 tests   (setarch -R, as documented)
release     20/20 tests
coverage    89.1% lines, 100.0% functions, 76.7% branches (gate >=85% lines)
format, clang-tidy, symbols, public headers, layers, docs, bindings,
plan-refs, version, gate selftest: pass
```

VM startup: **0.774 ms/VM** (`bench/vm_start.c`, 1,000 create/free cycles, release build,
including `wrenMakeCallHandle("call()")`). Binding table: **3 rows**, all `test` module only.

## Deviations and notes

1. **Heap cap is advisory (ADR-0014).** Returning NULL from `reallocateFn` at the budget crashes Wren
   (`SEGV` in `wrenMethodBufferFill`). Hard memory isolation moves to process-level `RLIMIT_AS` in
   Phase 8; `vm_heap_bytes` is a meter and log signal.
2. **Handles live in `vm.c`, not `handles.c`.** There are no request/response handles yet; a separate
   file lands when HTTP owns handler closures.
3. **`#embed`-ed pure-Wren stdlib deferred.** No pure-Wren modules exist yet; the resolver serves the
   native declarations as C strings. The generator lands with the first `modules/*.wren` file in Phase 6.
4. **`test` module is test-only.** `Echo` is excluded from the public binding table in `BINDINGS.md`
   (see its “Test-only surface” note).
5. **I6 scope.** `wc_host_load` runs the script's top level before `wc_host_run`, so no VM entry happens
   while the loop is active. Loop-driven VM dispatch arrives with HTTP in Phase 5, where I6 is enforced.

## Review gate

- Native calls enter only through the static binding table; there is no dynamic lookup.
- Every host-created handle has a release path; 10,000 cycles leak nothing.
- The VM is never re-entered: the depth guard returns `WC_EBUSY` and the test asserts it.
- The Wren pin is unmodified; warnings are suppressed only on the vendored target.
