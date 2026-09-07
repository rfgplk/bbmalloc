#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys

OBJDUMP = {
    "x86": "objdump",
    "arm32": "/usr/gcc-linaro/bin/arm-none-linux-gnueabihf-objdump",
    "arm64": "/usr/gcc-linaro-aarch64/bin/aarch64-none-linux-gnu-objdump",
}

REG = {
    "arm32": r"(?<![\w.#$])(?:q(?:[0-9]|1[0-5])|d(?:[0-9]|[12][0-9]|3[01])|s(?:[0-9]|[12][0-9]|3[01]))(?![\w.])",
    "arm64": r"(?<![\w.#$])(?:v(?:[0-9]|[12][0-9]|3[01])|q(?:[0-9]|[12][0-9]|3[01])|d(?:[0-9]|[12][0-9]|3[01])|s(?:[0-9]|[12][0-9]|3[01])|h(?:[0-9]|[12][0-9]|3[01])|b(?:[0-9]|[12][0-9]|3[01]))(?![\w.])",
}

PATTERNS = {
    "x86": {
        "vector": (None, r"%(xmm|ymm|zmm)\d+|%mm[0-7](?!\w)|%st\("),
        "atomic": (r"^(lock|xchg|cmpxchg\w*|xadd\w*|mfence|lfence|sfence)$", None),
        "tls": (None, r"%fs:|%gs:|__tls_get_addr"),
    },
    "arm32": {
        "vector": (r"^(v\w+|f\w+)$", REG["arm32"]),
        "atomic": (r"^(ldrex\w*|strex\w*|ldaex\w*|stlex\w*|dmb|dsb)$", None),
        "tls": (None, r"tpidr|__tls_get_addr|__aeabi_read_tp"),
    },
    "arm64": {
        "vector": (r"^(f\w+|ld[1-4]r?|st[1-4]|fmov|movi|dup|ins|umov|addv|cnt)$", REG["arm64"]),
        "atomic": (r"^(ldxr\w*|stxr\w*|ldaxr\w*|stlxr\w*|ldadd\w*|swp\w*|cas\w*|ldclr\w*|ldset\w*|dmb|dsb)$", None),
        "tls": (None, r"tpidr|__tls_get_addr"),
    },
}

BRANCH = re.compile(r"^(b|bl|br|blr|b\.\w+|bx|blx|cb\w+|tb\w+|j\w+|call\w*|ret\w*|bcs\w*|bcc\w*|beq\w*|bne\w*|bhi\w*|bls\w*|bgt\w*|blt\w*|bge\w*|ble\w*|bmi\w*|bpl\w*|bvs\w*|bvc\w*|bal\w*)$")


def split(line):
    body = line.split("\t", 1)[1] if "\t" in line else ""
    body = body.split("//")[0].split(";")[0].split("@")[0]
    body = re.sub(r"\b[0-9a-f]+\s*<[^>]*>", "", body)
    body = re.sub(r"<[^>]*>", "", body).strip()
    if not body:
        return "", ""
    parts = body.split(None, 1)
    return parts[0], (parts[1] if len(parts) > 1 else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("object")
    ap.add_argument("--arch", choices=sorted(PATTERNS), required=True)
    ap.add_argument("--expect-nonzero", action="store_true")
    ap.add_argument("--show", type=int, default=5)
    a = ap.parse_args()

    out = subprocess.run([OBJDUMP[a.arch], "-d", "--no-show-raw-insn", a.object], capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        return 2
    insns = []
    for l in out.stdout.splitlines():
        if not re.match(r"^\s*[0-9a-f]+:\s", l):
            continue
        m, ops = split(l)
        if m:
            insns.append((l.strip(), m, ops))
    if not insns:
        print(f"{a.object}: no instructions disassembled")
        return 2

    total = 0
    for cls, (mrx, orx) in PATTERNS[a.arch].items():
        mre = re.compile(mrx) if mrx else None
        ore = re.compile(orx) if orx else None
        hits = []
        for raw, m, ops in insns:
            if mre and mre.match(m):
                if m == "xchg" and "(" not in ops:
                    continue
                hits.append(raw)
                continue
            if ore and not BRANCH.match(m) and ore.search(ops):
                hits.append(raw)
        total += len(hits)
        print(f"{a.object}: {cls:7s} {len(hits)}")
        for h in hits[: a.show]:
            print("    " + h)

    if a.expect_nonzero:
        ok = total > 0
        print("negative control:", "found forbidden instructions (gate can fail)" if ok else "FOUND NOTHING -- the gate is blind")
        return 0 if ok else 1
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
