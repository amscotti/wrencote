# ADR-0011: Trusted scripts in v0.1/v0.2

**Status:** Accepted
**Date:** 2026-10-23

## Context

Wren has no preemption, instruction-count hook, or interrupt. A handler can loop forever; the loop cannot fire a timer or kill the fiber. Claiming a sandbox for hostile scripts would be false.

## Decision

v0.1/v0.2 assume trusted scripts. Capabilities bound reach, not CPU. Hostile multi-tenancy is a process-per-tenant model with a watchdog in v0.3; `docs/security.md` states the limitation.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
