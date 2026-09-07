#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys

SYMBOLS = [
    "malloc",
    "calloc",
    "realloc",
    "free",
    "aligned_alloc",
    "memalign",
    "posix_memalign",
    "valloc",
    "pvalloc",
]


def defined_symbols(obj, nm):
    out = subprocess.run([nm, "-g", "--defined-only", obj], capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        sys.exit(2)
    found = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        kind = parts[-2]
        name = parts[-1]
        if kind in ("T", "W", "t", "w", "i", "u") and name in SYMBOLS:
            found.add(name)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("object")
    ap.add_argument("--nm", default=None)
    ap.add_argument("--expect-missing", action="store_true")
    args = ap.parse_args()

    nm = args.nm or shutil.which("nm")
    if nm is None:
        sys.stderr.write("check_shim: no nm on PATH\n")
        return 2

    found = defined_symbols(args.object, nm)
    missing = [s for s in SYMBOLS if s not in found]

    print("%s: defined %d/%d" % (args.object, len(found), len(SYMBOLS)))
    for s in SYMBOLS:
        print("  %-16s %s" % (s, "defined" if s in found else "MISSING"))

    if args.expect_missing:
        if found:
            print("negative control: symbols leaked without BBMALLOC_C_SHIM")
            return 1
        print("negative control: no C symbols without the shim (gate can fail)")
        return 0

    if missing:
        print("shim gate: partial interposition, %d symbol(s) missing" % len(missing))
        return 1
    print("shim gate: the whole aligned family is exported")
    return 0


if __name__ == "__main__":
    sys.exit(main())
