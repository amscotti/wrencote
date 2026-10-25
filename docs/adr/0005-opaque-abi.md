# ADR-0005: Opaque public handles and version script

**Status:** Accepted
**Date:** 2026-10-23

## Context

A library that exposes struct layout freezes its ABI and lets callers break invariants.

## Decision

Public headers expose opaque types; `wc_` is the only exported prefix; a linker version script and `nm -D --defined-only` enforce it. The sized host config is the single documented exception.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
