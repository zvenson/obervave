#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Live recording: each tests/rec/*.txt ends with "steps"; its "# expect X ..." lines are what track X must hold then
(note/gate per set step, T = tie). SMK-37 host build. Exit 1 on a failure."""
import os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    b = OUT / "rec-smk37"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(b),
                    str(ROOT / "firmware/src/obervave.c")], check=True)
    fails = 0
    for s in sorted((ROOT / "tests/rec").glob("*.txt")):
        text = s.read_text()
        want = {m[1]: m[2].strip() for m in re.finditer(r"^# expect ([A-D]) (.*)$", text, re.M)}
        out = subprocess.run([str(b), str(OUT / f"rec-{s.stem}"), str(s)], capture_output=True, text=True).stdout
        got = {m[1]: m[2].strip() for m in re.finditer(r"STEPS ([A-D]) (.*)$", out, re.M)}
        what = text.splitlines()[0].lstrip("# ")
        for trk, w in want.items():
            ok = got.get(trk) == w
            print(("  ok    " if ok else "  FAIL  ") + f"{s.stem} track {trk}: {what}" + ("" if ok else f"  (got {got.get(trk)}, want {w})"))
            fails += not ok
    print("recording: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
