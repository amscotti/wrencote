#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
proj=$(sed -nE 's/^project\(wrencote VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -1)
hdr=$(sed -nE 's/^#define WC_VERSION_STRING "([^"]+)".*/\1/p' include/wrencote/version.h | head -1)
if [[ -z "$proj" || "$proj" != "$hdr" ]]; then
    echo "version mismatch: project='$proj' header='$hdr'" >&2
    exit 1
fi
echo "version: ok ($proj)"
