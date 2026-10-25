# ADR-0007: v0.1 deferrals

**Status:** Accepted
**Date:** 2026-10-23

## Context

Scope discipline: v0.1 must ship a correct HTTP/1.1 server and embedding API before protocols and packages multiply the surface.

## Decision

TLS and HTTP/2 move to Phase 9 (v0.2). Outbound TCP/DNS, async filesystem, SQLite, a package registry, Windows, and untrusted multi-tenancy are deferred with a named seam.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
