# Toolchain

## Release floor

- CMake 4.0 or newer
- GCC 13+ or Clang 18+ (CMake falls back to `-std=c2x` when the compiler lacks `-std=c23`; the
  standard-library generator keeps a byte-array fallback, so `#embed` is not required)
- Ninja
- clang-format and clang-tidy, pinned to the same major version as CI (optional at configure time;
  the `format-check`/`tidy` targets only exist when the tools are found)

## Known toolchain issues

- **gcc <= 13 + ASan on aarch64 hangs** inside Wren string concatenation (`while (big.count <
  1048576) big = big + big` in `tests/data/fs_errors.wren` spins allocating; plain builds and
  clang+ASan pass, and gcc-14 is unaffected). CMake warns about this combination; prefer clang on
  arm64 or configure with `-DWRENCOTE_SANITIZE=OFF`.
- TSan cannot start inside unprivileged Docker on macOS hosts (`FATAL: ThreadSanitizer: unexpected
  memory mapping`); run the `tsan` preset on a real Linux host via `scripts/ci-local.sh`
  (`setarch -R`).

## Bootstrap environment used for local development

This machine has no root and Ubuntu 24.04's default GCC 13/CMake 3.28, so the bootstrap toolchain is
installed under `~/.local`:

```sh
python3 -m pip install --user cmake==4.4.4 clang-format==21.1.8 clang-tidy==21.1.6 gcovr
```

`zig cc` provides Clang 22 for C23 work without root. GCC 13 is kept as the second bootstrap
compiler and compiles in `-std=c2x` mode; CMake falls back automatically when `-std=c23` is
unavailable. GCC 13 cannot compile `#embed`, so the standard-library generator must keep its
byte-array fallback until the GCC 15 image is available.

## Versions observed on 2026-10-23

```
cmake 4.4.4
clang-format 21.1.8
clang-tidy 21.1.6
zig 0.17.0 (clang 22.1.8)
gcc 13.3.0
gcovr 8.7
```

## Missing locally

`cppcheck` and `valgrind` are not installed and require root. They are wired into CI and the local
gate reports them as skipped until the pinned image exists; `docs/toolchain.md` is updated when the
image digest is recorded.

## TSan on this kernel

ThreadSanitizer aborts with `unexpected memory mapping` unless ASLR is disabled. Run TSan tests as
`setarch $(uname -m) -R ctest --preset tsan`; `scripts/ci-local.sh` does this. The code is unaffected.
