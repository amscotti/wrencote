# ADR-0018: Asynchronous module I/O via deferred exchanges

**Status:** Proposed — bounds landed in the interim (10 s `Http`/`Sys.sleep` caps)
**Date:** 2026-10-26

## Context

The loop is single-threaded by design (ADR-0002) and Wren's foreign-method API is synchronous:
`Http.request(...)` must return the body and `Sys.sleep(ms)` must not return early. There is no
supported way to yield the calling fiber from C, so any blocking foreign call parks the entire
loop — one slow `Http.request` to a dead origin stalls every connection for the full timeout
(GAPS G18), and `invoke_handler` cannot answer a response after the handler returns because
`Req`/`Res` hold raw pointers into request-scoped storage (GAPS G16).

## Decision

Land the bounds now, and move to callback-style asynchronous module I/O in v0.3/v0.4:

1. **Interim (shipped):** `Http` defaults to a 10 s overall / 5 s connect timeout with
   `Http.setTimeout(ms)` for scripts that need longer; `Sys.sleep` is capped at 10 s. Blocking
   remains, but a single stuck call can no longer stall the loop for a minute.
2. **Deferred exchanges.** `wc_exchange` becomes a refcounted, generation-stamped object owning
   the request data and the response slot. A handler may return *without* responding; the host
   keeps the Wren closure and exchange alive. Completions re-enter Wren through a stored closure
   with a validity check (generation), replacing today's raw `Req`/`Res` pointers. SSE becomes a
   natural special case instead of a special path.
3. **HTTP via curl_multi on the loop.** No worker threads: `curl_multi_socket_action` registers
   its sockets and timers on the existing `wc_loop` (the `blocked_on` transport work in the
   0.3.0 pass is the shape of this integration). DNS uses curl's threaded resolver inside
   libcurl. New callback APIs — `Http.request(method, url, headers, body) { |res| … }` — with the
   synchronous forms retained for CLI/batch use.
4. **Timers for sleep.** `Sys.sleep(ms, fn)` schedules a loop timer and resumes via the deferred
   exchange; the synchronous form stays bounded.
5. **fs stays synchronous.** Local file I/O is page-cache fast and sandboxed via openat2; a worker
   pool (`wc_loop_post` on the already thread-safe wake fd) is a later option for network
   filesystems, not a v0.3 requirement.

## Consequences

- Handler lifetime rules change: the host must outlive in-flight exchanges and validate
  generations on completion; `invoke_handler` and the streaming path are reworked on `wc_exchange`.
- The module API grows a second, callback-based shape; docs must be explicit about which calls can
  park the loop.
- With bounds in place, the blocking behavior is a documented latency risk rather than an
  unbounded outage; revisit priority if multi-tenant hosting (v0.3, process per tenant) changes
  the calculus.
