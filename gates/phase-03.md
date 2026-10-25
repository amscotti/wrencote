# Phase 3 evidence — event loop, timers, transport

**Status:** passed (2026-10-23)
**Command:** `scripts/ci-local.sh` (exit 0)

## Delivered

| File | Purpose |
|---|---|
| `src/loop/poll.[ch]` | epoll backend with token-based dispatch, EINTR handling |
| `src/loop/timer.[ch]` | binary min-heap with add/remove/pop and next-deadline |
| `src/loop/loop.[ch]` | fd registry, deferred queue, eventfd wakeup, signalfd, stop, run/run-once |
| `src/loop/transport.[ch]` | `wc_transport_ops` and the plain socket implementation |

Tests: `test_poll`, `test_timer_heap`, `test_timers`, `test_loop_echo`, `test_wakeup`,
`test_fd_stability`, `test_signal`, `test_transport` (18 total with earlier phases).

## Gate results

```
ci-gcc:   build + ctest  -> 18/18 passed
ci-clang: build + ctest  -> 18/18 passed
tsan:     build + ctest  -> 18/18 passed (ASLR disabled for the TSan runtime)
format-check             -> ok
tidy                     -> ok
symbols                  -> ok (only wc_* exported)
public headers           -> ok
plan references          -> ok
version                  -> ok (0.1.0)
layers                   -> ok (loop -> core only)
docs                     -> ok
bindings                 -> 14 blocks well-formed; native table not present yet
coverage                 -> 89.9% lines (703/782), 100% functions
gate selftest            -> all seeded violations rejected
```

## Behavioural proofs

- **Echo:** 100,000 one-byte round-trips through `wc_loop_run_once` with a socketpair; every byte
  returned unchanged (test runs in ~0.6 s).
- **Timers:** strict ordering at 100/200/300 ns deadlines on a fake clock; cancellation removes a
  timer; past deadlines fire on the next turn; a real-clock 10 ms timer fires between 10 ms and
  100 ms.
- **FD stability:** 10,000 add/delete cycles leave the active-fd count at 1 (wakeup only) and the
  process fd count unchanged.
- **Wakeup:** a second thread calls `wc_loop_stop`; `wc_loop_run` returns, and a deferred task runs
  exactly once.
- **Signal:** `SIGUSR1` is blocked and delivered through signalfd; the loop stops.
- **Transport:** iovec write and read over a socketpair; closing through the ops sets `fd = -1`.

## Notes

- TSan fails on this kernel with "unexpected memory mapping" unless ASLR is disabled; the local gate
  runs TSan tests under `setarch $(uname -m) -R`. This is an environment quirk, not a code issue, and
  is recorded in `docs/toolchain.md`.
- cppcheck and valgrind remain skipped locally (not installed).
