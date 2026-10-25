#!/usr/bin/env python3
"""Check that every exported declaration has a documentation comment."""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    errors = []
    for path in sorted(glob.glob(os.path.join(ROOT, "include", "wrencote", "*.h"))):
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().splitlines()
        for index, line in enumerate(lines):
            if "WC_API" not in line or "(" not in line:
                continue
            if line.lstrip().startswith("#define"):
                continue
            window = lines[max(0, index - 4) : index]
            if not any("*/" in item or item.strip().startswith("*") for item in window):
                errors.append(f"{os.path.relpath(path, ROOT)}:{index + 1}: missing doc comment")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("docs: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
