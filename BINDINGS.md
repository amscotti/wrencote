# Wrencote — Language Surface and Native Bindings

**Status:** v2, revised after review · 2026-10-23 · Companion to `PLAN.md`
**Scope:** every module, class, method, and getter Wrencote adds to Wren, how each behaves, what it costs, and the rules that keep the surface idiomatic. `tools/check_bindings.py` treats this document, the `foreign` declarations in `modules/*.wren`, and the C table in `src/vm/bindings.c` as three views of one list.

---

## 1. Why this document exists

Wrencote's native surface *is* its language design. A script author never sees a `wc_` symbol, a file descriptor, an error code, or a length parameter; they see classes, getters, and methods that either read like Wren's core library or make the runtime feel like C in a costume. Every binding is specified here before it is implemented: signature, arity, semantics, failure behaviour, blocking behaviour, lifetime, and capability. The test is simple: **could a Wren programmer who has read only the language's documentation guess this API correctly?** If not, the API is wrong.

## 2. Idiom rules (normative)

- **B1 — Modules are lowercase nouns.** `fs`, `net`, `sys`, `json`, `router`. `http` is the outbound client namespace; `net` remains the server listener.
- **B2 — Classes are nouns, methods are verbs, properties are getters.** `req.path`, not `req.getPath()`. `res.send(...)`, not `res.response(...)`.
- **B3 — No C vocabulary in the language surface.** No `get_`/`set_`, no `wc_`, no `_t`, no `buf`, `len`, `fd`, `argc`, `NULL`. Names are Wren words: `path`, `body`, `headers`, `exists`.
- **B4 — Constructors are `construct new(...)` when a class has instances.** Classes that are pure namespaces (`System`, `Server`) are static-only; that is a deliberate Wren idiom, not an oversight.
- **B5 — No error or result codes.** No call returns an integer that must be compared to a constant. An HTTP status is protocol data, passed as a plain `Num` in 100–599; everything else either returns a value, returns `null` for absence, or aborts with a message.
- **B6 — Errors abort; absence is `null`.** Missing file, permission denied, path escape, size cap: abort with a prose message naming the path and the reason. `Fs.exists` is the one call that answers absence with `false`; it still aborts on a path that escapes the root.
- **B7 — Collections are `List` and `Map` of Wren values.** No opaque iterators. Foreign handles are reserved for resources with a lifetime (a request/response pair, a future socket or file handle).
- **B8 — Strings are UTF-8 by convention and byte-safe by construction.** No truncation at NUL. Binary payloads are the same `String` type until a `Buffer` class arrives with streaming in v0.3.
- **B9 — Numbers are doubles.** APIs that accept identifiers above 2^53 say so and reject them; `Json` enforces the range on decode.
- **B10 — Callbacks are `Fn` with a documented arity.** Request handler: 2. Router handler: 3. Nothing takes more; pass an object instead.
- **B11 — No hidden blocking in getters.** A getter never blocks. `Fs.*` is blocking in v0.1 and documented as such; the p99 and soak targets are measured with handlers that do not touch `fs`. `Res.send` never blocks on the socket: it validates, copies, and buffers.
- **B12 — Resources have idempotent `stop()`/`close()`.** Using a stopped resource aborts with a named message. `Server.stop()` before `serve` is a no-op; `serve` after `stop` aborts.
- **B13 — Request and response objects are ephemeral; the values they return are not.** `Req`/`Res` are valid only during the handler call; using one later aborts with "request is no longer valid" or "response is no longer valid". Every value returned by a `Req` getter (path, headers, body) is an ordinary VM-owned Wren value and may be stored indefinitely. Store the values, not the request.
- **B14 — Capabilities are part of the API.** Each native module maps to one `WC_CAP_*` bit; importing a disabled module is a compile-time error naming the capability. Pure-Wren modules (`json`, `router`) touch no host resource and are always importable.
- **B15 — The surface is versioned.** Behaviour may not change silently between minor versions; breaking changes need an ADR and a deprecation note here.
- **B16 — Abort messages are prose.** A message names the resource and the reason ("path escapes the filesystem root: ../etc/passwd"); `WC_*` statuses appear only in the host's structured log line.
- **B17 — Suspending inside a handler is not allowed.** `Fiber.suspend`, `Fiber.yield`, and `Fiber.transfer` inside a request handler abort with "cannot suspend inside a handler". The host detects a suspended fiber on return from `wc_vm_call`.

## 3. Module map

| Module | Capability | Kind | Entry points | Status |
|---|---|---|---|---|
| `net` | `WC_CAP_NET` | native | `Server`, `Req`, `Res` | v0.1 (HTTP/1.1), v0.2 (HTTP/2) |
| `http` | `WC_CAP_NET` | native (libcurl) | `Http` | optional |
| `fs` | `WC_CAP_FS` (requires `fs_root`) | native | `Fs` | v0.1 |
| `sys` | `WC_CAP_SYS` | native | `Sys` | v0.1 |
| `json` | none | pure Wren | `Json` | v0.1 |
| `router` | none | pure Wren | `Router` | v0.1 |

## 4. Binding entries

Every entry below has a machine-readable block. `check_bindings.py` parses these blocks, the `foreign` declarations in `modules/*.wren`, and the C table, and fails if any of the three disagrees on name, arity, or staticness. `test_only` entries are excluded from release builds.

### 4.1 `Server`

```binding
module=net class=Server signature=serve(_) static=true arity=1
returns=null
errors=aborts if no listener is bound; aborts if a handler is already installed
blocking=no
since=0.1
```

`Server.serve(handler)` installs the request handler. The **host** owns the listener (`wc_host_serve` / CLI `--listen`); the script never chooses a port. `handler` is a `Fn` of arity 2: `|req, res|`. A second call aborts with "a handler is already installed". If the script never calls `serve`, the host answers every request with 503.

`Server.running` is a `Bool` getter: true between `serve` and `stop`.

```binding
module=net class=Server signature=stop() static=true arity=0
returns=null
errors=none; idempotent; no-op before serve
blocking=no
since=0.1
```

`Server.stop()` stops accepting, drains in-flight responses for the grace period, and returns `null`. Safe to call from inside a handler: the current response completes, then the loop exits.

### 4.2 `Req`

| Member | Type | Semantics |
|---|---|---|
| `method` | `String` | Uppercase method. |
| `path` | `String` | Percent-decoded path, no query, begins with `/`. |
| `query` | `Map` | Decoded query parameters; duplicate keys: last wins. |
| `headers` | `Map` | Lowercase names; duplicates joined with `", "` except `cookie`, joined with `"; "`. |
| `body` | `String` | Request body; empty when absent; capped by `max_body_bytes`. |
| `httpVersion` | `String` | `"1.1"` or `"2"`. |

```binding
module=net class=Req signature=method static=false arity=0
returns=String
errors=none
blocking=no
since=0.1
```

(The same block shape applies to `path`, `query`, `headers`, `body`, `httpVersion`; `check_bindings.py` requires one per member.)

All getters return VM-owned Wren values (B13); none is a pointer into the request arena.

`Req.rawHeaderCount`, `rawHeaderName(index)`, `rawHeaderValue(index)`, `rawQueryCount`, `rawQueryKey(index)`, and `rawQueryValue(index)` are implementation detail used by the Wren-level `headers` and `query` builders. They are public only because Wren has no private foreign methods; user code should use `headers` and `query`.

### 4.3 `Res`

```binding
module=net class=Res signature=send(_,_) static=false arity=2
returns=null
errors=aborts if already sent; aborts if status outside 100..599; aborts if body exceeds max_response_bytes
blocking=no (validates, copies, buffers)
since=0.1
```

`send(status, body)` finalizes the response. The head and body are copied into a connection-owned buffer at call time, so a deferred write never points into the arena or the VM heap. Calling twice aborts "response already sent". `res.send(204, "")` is the bodyless form; Wren has no optional arguments.

```binding
module=net class=Res signature=header(_,_) static=false arity=2
returns=null
errors=aborts after send; aborts on connection-specific names under HTTP/2
blocking=no
since=0.1
```

`header(name, value)` adds a response header. Names are lowercased on the wire; under HTTP/2, `connection`, `proxy-connection`, `keep-alive`, `transfer-encoding`, and `upgrade` abort with a message. If no `Content-Type` is set, the host sends `text/plain; charset=utf-8`.

`Res.sent` is a `Bool` getter.


```
module=net class=Res signature=sseStart(_) static=false arity=1
args=String content type
returns=null; starts a chunked HTTP/1.1 response
errors=aborts on HTTP/2, already-sent responses, or non-2xx status

module=net class=Res signature=sseSend(_,_) static=false arity=2
args=String event, String data
returns=null; sends one SSE frame and flushes it
errors=aborts if the stream is not open or the connection failed

module=net class=Res signature=sseClose() static=false arity=0
returns=null; writes the terminal chunk and marks the connection closing
```

### 4.4 `Fs`

```binding
module=fs class=Fs signature=read(_) static=true arity=1
returns=String (up to 1 MiB)
errors=aborts on ENOENT, EACCES, directory, path escape, or size cap
blocking=yes (documented; stalls the loop for the syscall duration)
since=0.1
```

`read(path)` returns the file contents. Size is checked with `st_size` before allocation; a file larger than the cap aborts with the cap in the message, never truncates.

```binding
module=fs class=Fs signature=write(_,_) static=true arity=2
returns=null
errors=aborts on EACCES, path escape, or content over 1 MiB
blocking=yes
since=0.1
```

`write(path, contents)` creates or truncates. Returns `null`; a byte count would expose a C-shaped, UTF-8-ambiguous value that nothing consumes.

```binding
module=fs class=Fs signature=exists(_) static=true arity=1
returns=Bool
errors=aborts on path escape or NUL bytes; false only for ENOENT/ENOTDIR inside the root
blocking=yes
since=0.1
```

```binding
module=fs class=Fs signature=list(_) static=true arity=1
returns=List<String>
errors=aborts if not a directory, or on path escape
blocking=yes
since=0.1
```

```
module=fs class=Fs signature=isDir(_) static=true arity=1
args=String path
returns=Bool
errors=aborts when the path is missing or escapes the root
```

Path rules: relative to `fs_root`; absolute paths rejected; resolved with `openat2(RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS)`; NUL rejected; 4 KiB cap. On kernels without `openat2`, a component-wise `openat` walk with `O_NOFOLLOW|O_DIRECTORY` is the documented fallback.

### 4.5 `Sys`

```binding
module=sys class=Sys signature=env(_) static=true arity=1
returns=String or null
errors=none for absence
blocking=no
since=0.1
```

`Sys.args` returns `List<String>`. `Sys.clock` returns monotonic seconds since host start. Note that core `System.clock` is **CPU time** (`clock()/CLOCKS_PER_SEC`), not wall time; `Sys.clock` is the wall-duration clock.

### 4.6 `Http` (native, optional libcurl)

```
module=http class=Http signature=request(_,_,_,_) static=true arity=4
args=String method, String url, String headers, String|null body
returns=String response body (status code is not exposed yet)
blocking=yes, up to 60 s
capability=WC_CAP_NET
notes=headers are newline-separated `Name: value` lines; redirects followed; TLS verification on; only http/https URLs (file:// and other libcurl protocols rejected); response cap 8 MiB / 10,000 lines

module=http class=Http signature=streamLines(_,_,_,_) static=true arity=4
args=String method, String url, String headers, String|null body
returns=List<String> response lines, including blank SSE separators
blocking=yes; the HTTP read is streaming but the list arrives at completion; 8 MiB / 10,000 line cap
capability=WC_CAP_NET
```

`Http` is compiled only when CMake finds libcurl; otherwise `import "http"` is denied.
### 4.7 `Json` (pure Wren)

```binding
module=json class=Json signature=encode(_) static=true arity=1
returns=String
errors=aborts on non-finite numbers, non-String Map keys, or unsupported types
blocking=no
since=0.1
```

`encode` handles `Null`, `Bool`, `Num`, `String`, `List`, `Map` with string keys. Object key order is unspecified (Wren `Map` order is undefined); tests compare decoded values, not bytes.

```binding
module=json class=Json signature=decode(_) static=true arity=1
returns=the decoded value
errors=aborts with a byte offset on malformed input, depth > 64, input > 1 MiB, or an integer outside ±2^53
blocking=no
since=0.1
```

### 4.8 `Router` (pure Wren)

```binding
module=router class=Router signature=add(_,_,_) static=false arity=3
returns=this (chaining builder)
errors=aborts on duplicate method+pattern or handler arity != 3
blocking=no
since=0.1
```

```binding
module=router class=Router signature=dispatch(_,_) static=false arity=2
returns=Bool
errors=propagates handler aborts
blocking=no
since=0.1
```

Patterns are exact or `:name` segments; one optional trailing slash matches; `Router.matches(method, path)` answers "did the path match" so a caller can return 405. Handlers take `|req, res, params|`.

## 5. Lifetime, errors, and blocking at a glance

| Situation | Behaviour |
|---|---|
| Using `Req`/`Res` after the handler returns | Abort: "request is no longer valid" / "response is no longer valid" |
| Storing `req.path`, `req.headers[...]`, `req.body` | Fine — these are VM-owned Wren values |
| `res.send` twice / `header` after `send` | Abort |
| `Fiber.suspend`/`yield`/`transfer` in a handler | Abort: "cannot suspend inside a handler" |
| Handler throws | 500 if nothing sent; log with line and stack |
| Missing file | `Fs.read` aborts; `Fs.exists` returns `false` |
| Path escapes root | Every call aborts, including `exists` |
| Oversized file/body/response | Abort before allocation, message states the cap |
| Import without capability | Compile error naming the capability |
| Blocking | `Fs.*`, `Http.*`, and `Sys.sleep` block, documented; `net` never blocks |

Recovering from an abort uses Wren's only catch: `var err = Fiber.new { contents = Fs.read(p) }.try()`. `err` is `null` on success and the message otherwise; the fiber cannot be reused. Prefer this over `exists` + `read`, which races.

## 6. Adding a binding

1. **Write the Wren declaration** in `modules/<module>.wren`, following B1–B17.
2. **Write the C function** in `src/modules/` or `src/server/`; read slots, validate, write one return value.
3. **Add one row** to `src/vm/bindings.c` with the arity, and `wc_slot_check` at the top of the function.
4. **Add the capability** if new: `WC_CAP_*`, resolver case, config validation, CLI token, docs.
5. **Add the binding block** to this document (the checker requires it).
6. **Tests:** C unit, Wren-level integration, error cases, capability denial, wrong-arity abort.
7. **Fuzz** if it parses bytes (paths, headers, JSON, frames).
8. **Measure** if it is on the request path.
9. **Run** `tools/check_bindings.py`, `check_no_plan_refs.sh`, clang-tidy.
10. **Idiom review:** a reviewer who writes Wren signs off in `gates/phase-NN.md`.

## 7. Worked example

```wren
import "net" for Server
import "fs" for Fs
import "json" for Json
import "router" for Router

var router = Router.new()

router.add("GET", "/health") { |req, res, params|
  res.send(200, "ok\n")
}

router.add("GET", "/users/:id") { |req, res, params|
  res.header("Content-Type", "application/json")
  res.send(200, Json.encode({"id": params["id"]}))
}

router.add("GET", "/readme") { |req, res, params|
  res.header("Content-Type", "text/plain")
  res.send(200, Fs.read("README.md"))
}

Server.serve { |req, res|
  if (!router.dispatch(req, res)) res.send(404, "not found\n")
}
```

No pointer, no length, no error constant, no port. Getters are properties; actions are methods; errors abort; absence is `null`; the router handler takes three arguments.

## 8. Rejected designs

| Rejected | Why | Instead |
|---|---|---|
| `Fs.read(path, buffer, length)` | C shape, out-parameter, manual sizing | `Fs.read(path)` returns a `String` |
| `{ok: Bool, value: ...}` | No tuples; error handling by convention | abort, or `null` for absence |
| `Fs.write` returning a byte count | C's `write()` shape, UTF-8-ambiguous, unused | return `null` |
| `req.getHeader("X")` | Verb prefix on a property | `req.headers["x"]` |
| `Server.serve(port, handler)` | Two owners of the listener | host binds; `Server.serve(handler)` |
| Exposing fds/pointers as `Num` | Unsafe and unguessable | foreign handle with `close()` |
| Six-argument callbacks | Unreadable call sites | pass an object; max arity 3 |
| Silent truncation at a cap | Data loss without a signal | abort with the cap in the message |
| `WC_EACCES` inside an abort message | C vocabulary in the language | prose message; status in the log |
| `Sys.clock` removed in favour of `System.clock` | Core `System.clock` is CPU time, not wall time | keep `Sys.clock`, document the difference |

## 9. Open questions

- `Fs.read` on a missing file: abort (current) vs `null`. Current answer: abort; `exists` is the predicate.
- Header map case: lowercase names (current). Documented; a case-insensitive lookup is a v0.2 decision.
- `Buffer` and streaming bodies: v0.3.
- Per-call bind address: rejected for v0.1; the host owns the listener.

## Test-only surface

The `test` module (`Echo`) is compiled into every build but is not part of the public surface. It exists to exercise foreign allocation, getters, static methods, arity checks, and the re-entry guard in unit tests. It is not documented as a binding block and must not be imported by user scripts; the CLI does not expose it.
