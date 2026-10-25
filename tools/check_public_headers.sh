#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
status=0
while IFS= read -r h; do
    while IFS= read -r inc; do
        case "$inc" in
            '<stddef.h>'|'<stdint.h>'|'<stdbool.h>'|'"wrencote/'*|'<wrencote/'*) ;;
            *) echo "$h: forbidden include $inc" >&2; status=1 ;;
        esac
    done < <(grep -E '^[[:space:]]*#[[:space:]]*include' "$h" | sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*//' || true)
done < <(find include/wrencote -name '*.h' | sort)
exit $status
