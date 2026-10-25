# ADR-0006: `wc_status` error model

**Status:** Accepted
**Date:** 2026-10-23

## Context

errno is thread-local, easily lost, and not composable; returning raw integers invites silent ignoring.

## Decision

Every fallible function returns `wc_status` with fixed integer values; values come out through pointers; the library never calls `abort()` or `exit()`. `[[nodiscard]]` marks every status-returning function.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
