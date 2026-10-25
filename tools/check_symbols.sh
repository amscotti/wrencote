#!/usr/bin/env bash
set -euo pipefail
lib=${1:?usage: check_symbols.sh <library>}
if [[ "$lib" == *.so* ]]; then
    mapfile -t syms < <(nm -D --defined-only "$lib" | awk '{print $3}' | sed 's/@.*//' | grep -v '^$' | sort -u)
else
    mapfile -t syms < <(nm --defined-only --extern-only "$lib" | awk '{print $3}' | sort -u)
fi
bad=()
for s in "${syms[@]}"; do
    [[ "$s" == wc_* ]] || bad+=("$s")
done
if ((${#bad[@]})); then
    printf 'unexpected exports in %s:\n' "$lib" >&2
    printf '  %s\n' "${bad[@]}" >&2
    exit 1
fi
echo "symbols: ok ($lib)"
