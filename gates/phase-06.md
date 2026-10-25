# Phase 6 gate — native fs/sys and pure-Wren JSON/router

**Date:** 2026-10-23
**Commit:** `8082e49`
**Tests:** 25 total (24 prior + `test_stdlib`)

## Delivered

| Area | Files | What it does |
|---|---|---|
| `fs` | `src/modules/fs.c`, `src/modules/fs.h` | `Fs.read/write/exists/list`, root-relative paths, 1 MiB caps, capability gating |
| `sys` | `src/modules/sys.c`, `src/modules/sys.h` | `Sys.env/args/clock` |
| JSON | `modules/json.wren` | encode/decode, cycle detection, UTF-8 byte handling, `\u` surrogate pairs, depth/size/safe-integer limits |
| Router | `modules/router.wren` | exact and `:param` matching, optional trailing slash, duplicate and arity checks, `dispatch`/`matches` |
| Embedding | `tools/gen_stdlib.py`, CMake custom command, `src/vm/stdlib.c` (generated) | one `{name, data, len}` table for `modules/*.wren` |
| Tests | `tests/integration/test_stdlib.c`, `tests/data/json_app.wren`, `router_app.wren`, `fs_app.wren`, `fs_errors.wren` | Wren-level integration for every module |
| Docs | `docs/stdlib.md` | module/capability table, path rules, blocking and error semantics |

## Filesystem security and behaviour

Paths are opened with
`openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS)`, `O_NOFOLLOW`, and a root
fd held by the host. A component-wise `openat(... O_NOFOLLOW | O_DIRECTORY)` walk is the fallback
when `openat2` is unavailable; the fallback is invoked directly by `test_stdlib` so it is not dead
code.

The escape matrix contains more than 12 named cases, all caught in `fs_app.wren`/`fs_errors.wren`:
`../etc/passwd`, absolute paths, `dir/../../etc/passwd`, symlink read/list/write, `..` list/write,
empty, overlong, and NUL paths. `Fs.exists` returns `false` only for `ENOENT`/`ENOTDIR` inside the
root; escapes abort. The 1 MiB round-trip writes and reads exactly 1,048,576 bytes; a 1,048,577-byte
write is refused before opening the file.

## JSON and router

`json_app.wren` round-trips a nested object containing quotes, backslashes, newlines, a Unicode
escape, booleans, null, and a list; malformed input and `Num.infinity` both abort with a message.
`router_app.wren` covers params, trailing slash, method mismatch, duplicate registration, and
handler arity.

The stdlib is genuinely embedded: `strings build/dev/libwrencote.a | grep 'class Json'` finds it,
and `test_stdlib` passes with `modules/` moved away after the build.

## Gates

```
ci-gcc      25/25 tests
ci-clang    25/25 tests
tsan        25/25 tests (setarch -R)
coverage    85.8% lines, 99.5% functions, 70.6% branches (gate >=85% lines)
format, clang-tidy, symbols, public headers, layers, docs, bindings,
plan-refs, version, gate selftest: pass
```

## Deviations and notes

1. **Generator, not `#embed`.** GCC 13 and Clang 18 do not implement C23 `#embed`. `tools/gen_stdlib.py`
   emits byte arrays with the same `{name, data, len}` contract. Revisit when the release toolchain
   is GCC 15+/Clang 19+.
2. **`Sys.args` is empty for library hosts.** The CLI in Phase 7 will populate it; the binding and
   return type already exist.
3. **`Sys.env` is gated by `WC_CAP_SYS`.** No separate `WC_CAP_ENV` bit in v0.1; the plan left that
   open. A host that grants `sys` grants environment reads.
4. **Pure-Wren JSON is not a libFuzzer target.** It is exercised by round-trip/error scripts; the C
   parser remains the fuzzed byte parser. A Wren-level mutation harness is deferred.
5. **No `docs/security.md` yet.** `openat2` removes path-resolution TOCTOU for the opened component;
   Landlock/seccomp containment remains Phase 8 as planned.

## Review gate

- Every path is checked for length, NUL, and absolute prefix before `openat2`; the root fd is the
  only base.
- `Fs.read` stats before allocating and refuses non-regular files and files over 1 MiB.
- `Fs.write` checks the content length before opening.
- JSON enforces the 1 MiB input and depth 64 limits before recursion.
- Router validates arity at registration; malformed patterns are handled by segment comparison.
