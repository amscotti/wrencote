#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version="${1:-0.2.1}"
prefix="$(mktemp -d)"
dist="dist/wrencote-${version}-linux-x86_64"
trap 'rm -rf "$prefix"' EXIT

if [ "${WRENCOTE_STATIC_RELEASE:-0}" = "1" ] && command -v musl-gcc >/dev/null 2>&1; then
    ./tools/build_musl.sh build/musl >/dev/null
    cmake --install build/musl --prefix "$prefix" >/dev/null
    cp build/musl/wrencote "$prefix/bin/wrencote"
else
    cmake --preset release >/dev/null
    cmake --build --preset release >/dev/null
    ctest --test-dir build/release --output-on-failure
    cmake --install build/release --prefix "$prefix" >/dev/null
fi
strip --strip-all "$prefix/bin/wrencote"
rm -rf "$dist"
mkdir -p "$dist/bin" "$dist/lib" "$dist/include"
cp "$prefix/bin/wrencote" "$dist/bin/"
cp "$prefix/lib/libwrencote.a" "$dist/lib/"
if [ -e "$prefix/lib/libwrencote.so.0.2.0" ]; then
    cp -a "$prefix/lib/libwrencote.so" "$prefix/lib/libwrencote.so.0" \
          "$prefix/lib/libwrencote.so.0.2.0" "$dist/lib/"
fi
cp -a "$prefix/include/wrencote" "$dist/include/"
cp README.md LICENSE CHANGELOG.md "$dist/"
cp -a docs "$dist/"
python3 tools/gen_sbom.py "$version" >"$dist/sbom.cdx.json"
(
    cd "$dist"
    sha256sum bin/wrencote lib/libwrencote.a lib/libwrencote.so.0.2.0 >SHA256SUMS
)
tar -C dist -czf "dist/wrencote-${version}-linux-x86_64.tar.gz" "wrencote-${version}-linux-x86_64"
echo "dist/wrencote-${version}-linux-x86_64.tar.gz"
