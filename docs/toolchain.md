# Toolchain

## Release floor

- CMake 4.0 or newer
- GCC 15+ or Clang 19+ (`#embed` and `constexpr` objects require Clang 19 / GCC 15)
- Ninja
- clang-format and clang-tidy, pinned to the same major version as CI

## Bootstrap environment used for local development

This machine has no root and Ubuntu 24.04's default GCC 13/CMake 3.28, so the bootstrap toolchain is
installed under `~/.local`:

```sh
python3 -m pip install --user cmake==4.4.4 clang-format==21.1.8 clang-tidy==21.1.6 gcovr
curl -sSLO https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz
tar -xf zig-x86_64-linux-0.17.0.tar.xz -C ~/.local/opt
ln -sf ~/.local/opt/zig-0.17.0/zig ~/.local/bin/zig
# zigcc wrapper: exec ~/.local/opt/zig-0.17.0/zig cc "$@"
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
