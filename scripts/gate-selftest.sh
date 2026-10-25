#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(git rev-parse --show-toplevel)"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"; rm -f src/core/_selftest_bad.c include/wrencote/_selftest_bad.h tests/_selftest_bad.c' EXIT

# symbol gate must reject a non-wc export
printf 'int not_wc(void) { return 0; }\n' > "$tmp/bad.c"
cc -shared -fPIC -o "$tmp/libbad.so" "$tmp/bad.c"
if bash tools/check_symbols.sh "$tmp/libbad.so" >/dev/null 2>&1; then
    echo "symbol gate did not fail on a seeded violation" >&2
    exit 1
fi

# layer gate must reject a core -> http edge
printf '#include "http/parser.h"\n' > src/core/_selftest_bad.c
if python3 tools/check_layers.py >/dev/null 2>&1; then
    echo "layer gate did not fail on a seeded violation" >&2
    exit 1
fi
rm -f src/core/_selftest_bad.c

# docs gate must reject an undocumented export
printf 'WC_API int not_documented(void);\n' > include/wrencote/_selftest_bad.h
if python3 tools/check_docs.py >/dev/null 2>&1; then
    echo "docs gate did not fail on a seeded violation" >&2
    exit 1
fi
rm -f include/wrencote/_selftest_bad.h

# plan-reference gate must reject a phase reference in code
printf '/* Pha%s 9 lives here */\n' se > tests/_selftest_bad.c
if bash tools/check_no_plan_refs.sh >/dev/null 2>&1; then
    echo "plan-ref gate did not fail on a seeded violation" >&2
    exit 1
fi
rm -f tests/_selftest_bad.c

echo "gate selftest: ok (all seeded violations rejected)"
