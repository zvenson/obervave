#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Hanging notes: each tests/hang/*.txt ends with every key, pad and MIDI note let go, then "voices"; no voice may
still be gated and no sound may still list a held key. Found by tests/fuzz_notes.py and by hand. SMK-37 build (the
scripts use its pads and outer keys). Exit 1 on a failure."""
import os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    b = OUT / "hang-smk37"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(b),
                    str(ROOT / "firmware/src/obervave.c")], check=True)
    fails = 0
    for s in sorted((ROOT / "tests/hang").glob("*.txt")):
        out = subprocess.run([str(b), str(OUT / f"hang-{s.stem}"), str(s)], capture_output=True, text=True).stdout
        v = [l.strip() for l in out.splitlines() if "VOICES" in l]
        ok = bool(v) and "gated=0 " in v[-1] and "held=0,0,0,0 " in v[-1]
        what = open(s).readline().lstrip("# ").strip()
        print(("  ok    " if ok else "  FAIL  ") + f"{s.stem}: {what}" + ("" if ok else f"  ({v[-1] if v else 'no VOICES'})"))
        fails += not ok
    print("hanging notes: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
