#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keys held through a program change, then let go: no note may keep sounding. A chord on 02 POLY BRASS, a program change
(MIDI, so both boards) while it is held, the keys up: 03 SYNC LEAD (MONO), 04 UNISON LD (UNISON), 01 DARK ARP (the
arp), and no change. Six seconds after the keys are up the output must be near silence (the effects' tails have died;
a hung voice stays at full level). Exit 1 on a failure."""
import os, subprocess, sys, wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + name + ("" if ok else f"  ({detail})"))
    fails += not ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for board, defs in (("fm1", []), ("smk37", ["-DBOARD_SMK37=1"])):
        bin_ = OUT / f"held-{board}"
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", *defs, '-DAPP_VER="test"',
                        "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(bin_),
                        str(ROOT / "firmware/src/obervave.c")], check=True)
        for case, what in (("mono", "to MONO (03)"), ("unison", "to UNISON (04)"), ("arp", "to the arp (01)"), ("none", "no change")):
            out = OUT / f"held-{board}-{case}"
            subprocess.run([str(bin_), str(out), str(ROOT / f"tests/held_switch_{case}.txt")], capture_output=True, check=True)
            w = wave.open(str(out) + ".wav")
            sr, ch = w.getframerate(), w.getnchannels()
            s = memoryview(w.readframes(w.getnframes())).cast("h")
            pk = lambda t0, t1: max(abs(v) for v in s[int(t0 * sr) * ch:int(t1 * sr) * ch])
            held, after = pk(4.0, 4.5), pk(10.8, 11.0)
            check(f"{board}: chord held, program changed {what}, keys up: silent 6 s later", after * 100 < held,
                  f"{after} of {held} while held")
    print("held notes across a program change: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
