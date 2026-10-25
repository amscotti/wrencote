#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd "$(git rev-parse --show-toplevel)"

for preset in ci-gcc ci-clang; do
    echo "== ${preset} =="
    cmake --preset "$preset" >/dev/null
    cmake --build --preset "$preset"
    ctest --preset "$preset" --output-on-failure
    # format-check/tidy targets only exist when the dev tools were found.
    if cmake --build --preset "$preset" --target help 2>/dev/null | grep -q format-check; then
        cmake --build --preset "$preset" --target format-check
    else
        echo "format-check: skipped (clang-format not found)"
    fi
    if cmake --build --preset "$preset" --target help 2>/dev/null | grep -q '^\.\.\. tidy'; then
        cmake --build --preset "$preset" --target tidy
    else
        echo "tidy: skipped (clang-tidy not found)"
    fi
    bash tools/check_symbols.sh "build/${preset}/libwrencote.so"
done

bash tools/check_public_headers.sh
bash tools/check_no_plan_refs.sh
bash tools/check_version.sh
python3 tools/check_layers.py
python3 tools/check_docs.py
python3 tools/check_bindings.py

echo "== tsan =="
cmake --preset tsan >/dev/null
cmake --build --preset tsan >/dev/null
setarch "$(uname -m)" -R ctest --preset tsan --output-on-failure

echo "== coverage =="
cmake --preset coverage >/dev/null
cmake --build --preset coverage >/dev/null
ctest --preset coverage --output-on-failure >/dev/null
cmake --build --preset coverage --target coverage

if command -v cppcheck >/dev/null 2>&1; then
    cppcheck --std=c23 --enable=warning,performance,portability --error-exitcode=1 src include
else
    echo "cppcheck: skipped (not installed)"
fi
if command -v valgrind >/dev/null 2>&1; then
    for binary in build/ci-gcc/test_*; do valgrind --error-exitcode=1 --leak-check=full --quiet "$binary"; done
else
    echo "valgrind: skipped (not installed)"
fi

bash scripts/gate-selftest.sh
echo "local CI passed"
