#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="${1:-build/musl}"
# musl's default include path lacks the Linux UAPI headers; idirafter keeps musl's
# libc headers first while making <linux/openat2.h> and friends visible.
CC=musl-gcc cmake -S . -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWRENCOTE_BUILD_TESTS=OFF \
    -DWRENCOTE_BUILD_TLS=OFF \
    -DWRENCOTE_ENABLE_TLS=OFF \
    -DWRENCOTE_ENABLE_HTTP2=OFF \
    -DCMAKE_C_FLAGS="-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--build-id=none"
cmake --build "$build"
