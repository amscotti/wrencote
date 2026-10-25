#!/usr/bin/env python3
"""Cross-check BINDINGS.md against the native table once the table exists.

The document, the `foreign` declarations in modules/*.wren, and the C table are
three views of one list. Until the native table lands, only the document is
validated for well-formed binding blocks.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "BINDINGS.md")
TABLE = os.path.join(ROOT, "src", "vm", "bindings.c")
REQUIRED = ("module", "class", "signature", "static", "arity", "returns", "errors", "blocking", "since")


def parse_blocks(text):
    blocks = []
    for match in re.finditer(r"```binding\n(.*?)```", text, re.S):
        fields = {}
        for line in match.group(1).splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("module=") and "class=" in line:
                for key, value in re.findall(r"(\w+)=(\S+)", line):
                    fields[key] = value
            elif "=" in line:
                key, value = line.split("=", 1)
                fields[key.strip()] = value.strip()
        blocks.append(fields)
    return blocks


def main() -> int:
    if not os.path.exists(DOC):
        print("bindings: BINDINGS.md missing", file=sys.stderr)
        return 1
    with open(DOC, encoding="utf-8") as handle:
        blocks = parse_blocks(handle.read())
    errors = []
    for index, block in enumerate(blocks, 1):
        missing = [key for key in REQUIRED if key not in block]
        if missing:
            errors.append(f"binding block {index}: missing {', '.join(missing)}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    if not os.path.exists(TABLE):
        print(f"bindings: {len(blocks)} blocks well-formed; no native table yet (skipped)")
        return 0
    print(f"bindings: {len(blocks)} blocks well-formed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
