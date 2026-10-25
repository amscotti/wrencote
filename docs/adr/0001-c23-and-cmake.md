# ADR-0001: C23, GCC 15+/Clang 19+, CMake 4 presets

**Status:** Accepted
**Date:** 2026-10-23

## Context

C23 is the current ISO C (9899:2024). `#embed` and `constexpr` objects require Clang 19 / GCC 15, which sets the release floor. CMake 4 presets and Ninja give one build description for local and CI.

## Decision

Build with `-std=c23`; GNU extensions are opt-in per file. The local bootstrap may use `-std=c2x` on GCC 13; that is a development allowance, not a supported floor.

## Consequences

The decision is recorded so later work does not relitigate it silently. Changing it requires a new ADR that names this one.
