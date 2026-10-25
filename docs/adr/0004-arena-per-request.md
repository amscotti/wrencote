# ADR-0004: Arena per in-flight request

**Status:** Accepted
**Date:** 2026-10-23

## Context

Per-request allocation with individual frees is the usual source of leaks and use-after-free. A reset-only arena removes the free path.

## Decision

One arena per in-flight request, reset only after the response is fully flushed. Response bytes are copied into a connection-owned buffer at `send()`; nothing points into an arena after the handler returns.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
