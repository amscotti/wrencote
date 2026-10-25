#!/usr/bin/env python3
"""Enforce the layer dependency matrix over the include graph."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

ALLOWED = {
    "core": set(),
    "loop": {"core"},
    "vm": {"core", "loop"},
    "http": {"core", "loop", "http2"},
    "server": {"core", "loop", "http", "vm", "modules", "tls"},
    "modules": {"core", "loop", "vm"},
    "tls": {"core", "loop"},
    "http2": {"core", "loop", "http", "tls"},
    "cli": {"core", "loop", "vm", "http", "server", "modules", "tls", "http2"},
}

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def main() -> int:
    errors = []
    for dirpath, _, names in os.walk(SRC):
        rel = os.path.relpath(dirpath, SRC)
        if rel == ".":
            continue
        layer = rel.split(os.sep)[0]
        if layer not in ALLOWED:
            errors.append(f"unknown layer directory: src/{layer}")
            continue
        for name in names:
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as handle:
                for lineno, line in enumerate(handle, 1):
                    match = INCLUDE.match(line)
                    if not match:
                        continue
                    inc = match.group(1)
                    if inc.startswith("wrencote/"):
                        continue
                    dep = inc.split("/")[0]
                    if dep == layer:
                        continue
                    if dep in ALLOWED and dep not in ALLOWED[layer]:
                        errors.append(
                            f"{os.path.relpath(path, ROOT)}:{lineno}: "
                            f"{layer} -> {dep} is not allowed"
                        )
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("layers: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
