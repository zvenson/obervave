#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A knob's speed is its detents a second: a slow turn moves one a detent however many detents a main-loop pass took
(a long pass: the screen drawing), a fast spin goes further however short the passes. tests/smk/knob_speed.txt on the
SMK-37 host build: K5 = CUTOFF on PERFORM. Exit 1 on a failure."""
import os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    b = OUT / "knob-smk37"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(b),
                    str(ROOT / "firmware/src/obervave.c")], check=True)
    out = subprocess.run([str(b), str(OUT / "knob"), str(ROOT / "tests/smk/knob_speed.txt")], capture_output=True, text=True).stdout
    cut = {int(m[1]): int(m[2]) for m in re.finditer(r"(\d+) ms  STATE .*?cutoff=(\d+)", out)}
    start = cut[3700] - 6                       # (CUTOFF, 0..127: 2 a detent)
    fails = 0
    def check(name, ok, detail):
        nonlocal fails
        print(("  ok    " if ok else "  FAIL  ") + name + ("" if ok else f"  ({detail})"))
        fails += not ok
    check("slow, a detent each 300 ms: one step (2) a detent", cut[3700] - start == 6, cut[3700] - start)
    check("slow, two detents in one long pass: two steps (4), not x3", cut[4800] - cut[3700] == 4, cut[4800] - cut[3700])
    check("a fast spin (50 a second), one detent a pass: it goes further than 5 detents (10)", cut[5100] - cut[4800] > 10, cut[5100] - cut[4800])
    print("knob speed: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
