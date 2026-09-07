#!/usr/bin/env python3

import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
DST = "/code/C++/micron/src/memory/allocation/bbmalloc"
DEPTH = "../../../"

BANNER = """GENERATED -- DO NOT EDIT ANY FILE IN THIS DIRECTORY.

These headers are written by /code/C++/bbmalloc/scripts/sync_to_micron.py --do-it, which copies
bbmalloc/src/*.hpp here and rewrites <micron/X> includes to "../../../X". Nothing else is changed:
namespace bb stays at global scope and micron aliases it in
memory/allocation/barebones/bb_alloc.hpp.

A fix made HERE is silently reverted by the next sync, and never sees the suite that makes a fix in
an allocator trustworthy -- upstream carries 33 hosted tests, 5 negative controls, 13 cross cells
behind a codegen gate, a hot-path gate, a C-shim gate, and the whole suite run under qemu on armv7
and aarch64. Fix it in /code/C++/bbmalloc/src, run `ninja bbmalloc_tests` and
`ninja -k 0 bbmalloc_cross` there, then re-sync.

The same rule ../ajax follows for micron's src/simd. See micron's BAREBONES.md section 10.
"""

_inc = re.compile(r'^(\s*#\s*include\s*)<micron/([^>]+)>(.*)$')


def convert(line: str) -> str:
    m = _inc.match(line.rstrip("\n"))
    if m:
        return f'{m.group(1)}"{DEPTH}{m.group(2)}"{m.group(3)}\n'
    return line if line.endswith("\n") else line + "\n"


def main() -> int:
    if "--do-it" not in sys.argv:
        print("dry run: pass --do-it to write into", DST)
    os.makedirs(DST, exist_ok=True) if "--do-it" in sys.argv else None
    n = 0
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".hpp"):
            continue
        with open(os.path.join(SRC, name)) as f:
            out = [convert(line) for line in f]
        if "--do-it" in sys.argv:
            with open(os.path.join(DST, name), "w") as f:
                f.write("".join(out))
        n += 1
    if "--do-it" in sys.argv:
        with open(os.path.join(DST, "README"), "w") as f:
            f.write(BANNER)
    print(f"{'synced' if '--do-it' in sys.argv else 'would sync'} {n} headers from {SRC} -> {DST}")
    print("micron side is already wired (ARCHITECTURE.md section 16, Phase 8). Re-verify after a sync:")
    print("  ./bin/duck batch verify_compile_barebones.duck && sh scripts/check_kernel_object.sh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
