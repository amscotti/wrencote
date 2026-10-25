#!/ usr / bin / env python3
"""Generate a C table from modules/*.wren.

GCC 13 and Clang 18 do not implement C23 #embed, so this emits byte arrays with
the same {name, data, len} table the loader consumes. Revisit when the release
toolchain supports #embed (GCC 15+/Clang 19+).
"""
import os
import sys


def c_identifier(name):
    cleaned = "".join(ch if ch.isalnum() else "_" for ch in name)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: gen_stdlib.py <modules-dir> <output.c>", file=sys.stderr)
        return 2
    modules_dir, output = sys.argv[1], sys.argv[2]
    names = sorted(name[:-5] for name in os.listdir(modules_dir) if name.endswith(".wren"))
    if not names:
        print("gen_stdlib: no .wren modules found", file=sys.stderr)
        return 1
    chunks = ['#include "vm/stdlib.h"\n']
    for name in names:
        path = os.path.join(modules_dir, name + ".wren")
        with open(path, "rb") as handle:
            data = handle.read()
        if b"\0" in data:
            print(f"gen_stdlib: {path} contains NUL", file=sys.stderr)
            return 1
        ident = "wc_stdlib_" + c_identifier(name)
        chunks.append(f"static const unsigned char {ident}[] = {{")
        chunks.append(",".join(str(byte) for byte in data))
        chunks.append(",0};\n")
    chunks.append("const wc_stdlib_module wc_stdlib_modules[] = {")
    for name in names:
        ident = "wc_stdlib_" + c_identifier(name)
        chunks.append(f'    {{"{name}", {ident}, sizeof {ident} - 1}},')
    chunks.append("};\n")
    chunks.append("const size_t wc_stdlib_module_count = "
                  "sizeof wc_stdlib_modules / sizeof wc_stdlib_modules[0];\n")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(chunks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
