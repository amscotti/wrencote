#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
if grep -RInE '\b(Phase|phase|Task|task)[ _-]?[0-9]+\b' src include tests tools scripts 2>/dev/null; then
    echo "plan or task references found in code" >&2
    exit 1
fi
echo "plan refs: ok"
