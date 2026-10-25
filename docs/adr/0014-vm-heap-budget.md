# ADR-0014: The VM heap budget is advisory; hard limits are process-level

**Status:** Accepted
**Date:** 2026-10-23

## Context

Wren's allocator callback is not an OOM-aware allocator. Returning NULL when a heap cap is exceeded
corrupts the VM (observed as a SEGV in `wrenMethodBufferFill` during a capped heap test). Wren has no
instruction-count hook and no recoverable allocation failure, so a hard in-process memory cap cannot be
enforced without patching the VM.

## Decision

`wc_vm` tracks heap usage and sets `over_budget` when the configured `vm_heap_bytes` is exceeded, but
it never fails an allocation for budget reasons. The budget is a diagnostic and a deployment signal.
Hard memory isolation is process-level: Phase 8 applies `RLIMIT_AS` (and the sandbox) to the server
process, and the v0.3 multi-tenant model uses a process per tenant.

## Consequences

- The `wc_host_config.vm_heap_bytes` field is a budget, not a kill switch; docs must say so.
- A runaway script can exhaust host memory until the process limit/OS kills it. This is consistent with
  the trusted-scripts statement in ADR-0011.
- `wc_vm_over_budget` exists for tests and future metrics; no script-visible behavior depends on it.
- Revisit if Wren gains a recoverable OOM or an instruction hook.
