# ADR-0003: Capability-gated module resolution

**Status:** Accepted
**Date:** 2026-10-23

## Context

Wren has no ambient I/O; the host decides what exists. A module that is not granted must fail at import, not at call time.

## Decision

Each native module maps to one `WC_CAP_*` bit. Unknown module names are denied. Pure-Wren modules carry no capability because they touch no host resource.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
