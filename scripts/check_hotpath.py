#!/usr/bin/env python3

import argparse
import re
import shutil
import subprocess
import sys

FUNCTIONS = ["bb_hot_alloc", "bb_hot_free", "bb_hot_query"]
COLD = re.compile(r"__slow|__cold|__report|__halt|__bad_free|__redzone_trip|small_alloc_slow|large_alloc_slow|"
                  r"__sheet_emptied|release_sheet|flush_spare|__region_slow|__alloc_slow|__alloc_aligned_slow|"
                  r"handle_bad_free|handle_redzone_trip|__attach|init\(|halt|write_diag|memset|memcpy")
COND_X86 = re.compile(r"^j(?!mp)\w+$")
COND_ARM = re.compile(r"^(b\.\w+|cb\w+|tb\w+|b(eq|ne|cs|cc|hi|ls|gt|lt|ge|le|mi|pl|vs|vc|hs|lo))$")
CALL = re.compile(r"^(call\w*|bl|blx)$")


def disassemble(obj, objdump):
    out = subprocess.run([objdump, "-dr", "--no-show-raw-insn", "-C", obj], capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        sys.exit(2)
    return out.stdout


RELOC = re.compile(r"^\s*[0-9a-f]+:\s+R_\w+\s+(.+)$")


def split_functions(text):
    funcs = {}
    name = None
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            name = m.group(1)
            funcs[name] = []
            continue
        if name is None:
            continue
        r = RELOC.match(line)
        if r:
            if funcs[name]:
                funcs[name][-1] = (funcs[name][-1][0], r.group(1))
            continue
        if not re.match(r"^\s*[0-9a-f]+:\s", line):
            continue
        body = line.split(":", 1)[1].strip()
        funcs[name].append((body, None))
    return funcs


def analyse(body):
    branches = 0
    calls = []
    for insn, reloc in body:
        parts = insn.split(None, 1)
        if not parts:
            continue
        mn = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        if COND_X86.match(mn) or COND_ARM.match(mn):
            branches += 1
            continue
        target = None
        if reloc is not None and "(" in reloc:
            target = reloc
        elif CALL.match(mn):
            target = rest
        if target is not None and not COLD.search(target):
            calls.append(target)
    return branches, calls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("object")
    ap.add_argument("--objdump", default=None)
    ap.add_argument("--max-branches", type=int, default=40)
    ap.add_argument("--expect-fail", action="store_true")
    args = ap.parse_args()

    objdump = args.objdump or shutil.which("objdump")
    if objdump is None:
        sys.stderr.write("check_hotpath: no objdump on PATH\n")
        return 2

    funcs = split_functions(disassemble(args.object, objdump))
    failed = False
    for fn in FUNCTIONS:
        body = funcs.get(fn)
        if body is None:
            print("%-14s MISSING" % fn)
            failed = True
            continue
        branches, calls = analyse(body)
        ok = branches <= args.max_branches and not calls
        print("%-14s insns=%-4d cond-branches=%-3d hot-calls=%d %s" % (fn, len(body), branches, len(calls),
                                                                     "ok" if ok else "FAIL"))
        for c in calls:
            print("    call -> %s" % c)
        if not ok:
            failed = True

    if args.expect_fail:
        if failed:
            print("negative control: the fast path is not inlined (gate can fail)")
            return 0
        print("negative control: BB_HOT_GATE_NEGATIVE left the fast path inlined")
        return 1
    if failed:
        print("hot-path gate: a fast path calls out or carries too many branches")
        return 1
    print("hot-path gate: every fast path is one inlined body with no hot calls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
