# ADR-0002: Single-threaded loop owns the VM

**Status:** Accepted
**Date:** 2026-10-23

## Context

Wren's C API refuses re-entry from a foreign method (`wrenCall` asserts `numFrames == 0`). A multi-threaded VM would need a lock around every call and would not remove the re-entry rule.

## Decision

One loop thread owns every socket and the VM. The loop enters the VM only when idle. CPU-bound or blocking work does not get a thread in v0.1.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
