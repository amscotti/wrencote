# ADR-0014: The VM heap budget is advisory; hard limits are process-level

**Status:** Accepted
**Date:** 2026-10-23

## Context

Wren's allocator callback is not an OOM-aware allocator. Returning NULL when a heap cap is exceeded
corrupts the VM (observed as a SEGV in `wrenMethodBufferFill` during a capped heap test). Wren has no
instruction-count hook and no recoverable allocation failure, so a hard in-process memory cap cannot be
enforced without patching the VM.

## Decision

`wc_vm` tracks heap usage and derives `over_budget` live from `heap_used > heap_limit`; it still never
fails an allocation for budget reasons. In addition, the host treats the budget as a request-level
shedding signal:

- Before dispatching a handler, if the VM is over budget, the host forces a collection
  (`wc_vm_collect`) and re-checks.
- If the heap is still over budget, the request is answered with `503` and no handler runs.
- A `vm_heap_bytes` smaller than the VM's baseline heap is rejected at startup (`WC_EINVAL`) rather
  than starting permanently over budget.
- A script that retains over-budget memory indefinitely (e.g. an unbounded module-global cache) will
  keep receiving `503`; recovery requires freeing that memory or restarting. The budget remains
  advisory at the allocator level, so it is not a hard memory cap.

Hard memory isolation is process-level: Phase 8 applies `RLIMIT_AS` (and the sandbox) to the server
process, and the v0.3 multi-tenant model uses a process per tenant.

## Consequences

- The `wc_host_config.vm_heap_bytes` field is a budget that sheds requests, not a hard kill switch at
  the allocator; docs must say so.
- A runaway script can still exhaust host memory until the process limit/OS kills it, and can deny
  itself service (503) by retaining memory above the budget. This is consistent with the
  trusted-scripts statement in ADR-0011.
- `wc_vm_over_budget` is a live predicate used by the host; tests cover that frees and GC clear it.
- Revisit if Wren gains a recoverable OOM or an instruction hook.
