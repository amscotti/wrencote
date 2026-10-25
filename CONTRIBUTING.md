# Contributing

## Build and test

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Run the full local gate before sending a change:

```sh
scripts/ci-local.sh
```

## Rules

- C23, 4-space indent, 100 columns, `clang-format` enforced.
- Public symbols use the `wc_` prefix; functions that allocate or return `wc_status` are documented
  and marked `[[nodiscard]]` where applicable.
- No plan or task references in code, tests, or comments; `tools/check_no_plan_refs.sh` enforces it.
- One logical change per commit, Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`, `build:`).
- New native bindings must be added to `BINDINGS.md`; `tools/check_bindings.py` checks the three-way
  match once the binding table exists.
