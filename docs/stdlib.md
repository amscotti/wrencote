# Standard library and native modules

Wren's core library has no I/O. Everything below is provided by Wrencote through the module
resolver; importing a module without its capability fails at compile time with `WC_EPERM`.

| Module | Capability | Kind | Contents |
|---|---|---|---|
| `net` | `WC_CAP_NET` | native | `Server`, `Req`, `Res` |
| `fs` | `WC_CAP_FS` + `fs_root` | native | `Fs.read/write/exists/list` |
| `sys` | `WC_CAP_SYS` | native | `Sys.env/args/clock` |
| `json` | none | pure Wren | `Json.encode/decode` |
| `router` | none | pure Wren | `Router.add/dispatch/matches` |

The two pure-Wren modules live in `modules/*.wren` and are embedded at build time by
`tools/gen_stdlib.py`. The generator emits a `{name, data, len}` table rather than using C23
`#embed`, because the bootstrap toolchain (GCC 13 / Clang 18) does not implement it. The table is
the same contract `#embed` would feed.

## `fs`

Paths are relative to `fs_root`; absolute paths, `..` escapes, NUL bytes, and symlinks that leave the
root are rejected before any file is opened. Resolution uses
`openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS)`; on kernels without
`openat2`, a component-wise `openat` walk with `O_NOFOLLOW | O_DIRECTORY` is the fallback.

| Call | Result | Notes |
|---|---|---|
| `Fs.read(path)` | `String` | up to 1 MiB; directories and larger files abort |
| `Fs.write(path, contents)` | `Null` | creates/truncates; contents up to 1 MiB |
| `Fs.exists(path)` | `Bool` | `false` only for `ENOENT`/`ENOTDIR` inside the root; an escape aborts |
| `Fs.list(path)` | `List<String>` | directory entries, excluding `.` and `..` |

All four calls block the loop for the syscall and file I/O. They are intended for configuration and
small assets, not for serving large files.

## `sys`

| Call | Result | Notes |
|---|---|---|
| `Sys.env(name)` | `String` or `Null` | reads the process environment; absence is `null` |
| `Sys.args` | `List<String>` | CLI arguments; empty for an embedded host |
| `Sys.clock` | `Num` | monotonic seconds since host construction |

`Sys.clock` is wall-duration time. Core `System.clock` is CPU time and is not a substitute.

## `http`

Optional libcurl-backed outbound client. Requires `WC_CAP_NET`; denied unless
`--allow net` is passed. Both calls block the loop for up to 60 seconds.

```wren
import "http" for Http
var body = Http.request("GET", "https://example.com/", "", null)
var lines = Http.streamLines("GET", "https://example.com/", "", null)
```

`headers` is a newline-separated `Name: value` string. Only `http://` and `https://` URLs are accepted; `file://`, FTP, and other libcurl
protocols are rejected even when `--allow net` is granted. TLS verification and
redirects use libcurl defaults. Responses are capped at 8 MiB and 10,000 lines. `streamLines` returns the response split on
newlines after completion; it is suitable for SSE parsing but is not a
Wren-callback streaming API.

## `json`

`Json.encode(value)` accepts `Null`, `Bool`, `Num`, `String`, `List`, and `Map` with `String` keys.
It aborts on non-finite numbers, cycles, non-String keys, and unsupported types. Object key order is
the map's iteration order and is not stable.

`Json.decode(text)` parses standard JSON (objects, arrays, strings, numbers, booleans, null). It
aborts with a byte offset for malformed input, depth over 64, input over 1 MiB, or an integer outside
`±2^53`. Surrogate pairs are combined; unpaired surrogates abort.

## `router`

`Router.new().add(method, pattern, handler)` returns the router for chaining. Patterns are exact
segments or `:name` parameters; one trailing slash is ignored on both pattern and path. A handler
must have arity 3 (`|req, res, params|`); duplicate `method + pattern` pairs abort.

`router.dispatch(req, res)` returns `true` when a route matched and called the handler, otherwise
`false`. `router.matches(method, path)` performs the same match without calling a handler, which lets
a caller distinguish 404 from 405.
