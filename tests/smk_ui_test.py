#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SMK-37 port of OberVave: Copyright (C) 2026 zvenson
"""The SMK-37 mapping on the host (tests/host.c with BOARD_SMK37): the step pads, REC on the pads, the fader banks,
the wheels, the outer keys. Builds build/host/obervave-smk37-test, runs tests/smk/*.txt, checks what comes out
(MIDI out, the dumped state, the pad colours). Exit 1 on a failure.

  python3 tests/smk_ui_test.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"
BIN = OUT / "obervave-smk37-test"
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + name + ("" if ok else f"  ({detail})"))
    fails += not ok


def run(script):
    out = subprocess.run([str(BIN), str(OUT / f"smk-{script}"), str(ROOT / "tests" / "smk" / f"{script}.txt")],
                         capture_output=True, text=True, check=True).stdout
    dumps = {}
    for ln in out.splitlines():
        m = re.match(r"\s*(\d+) ms  (PADS|STATE|STEPS)(.*)$", ln)
        if m:
            dumps.setdefault(int(m[1]), {})[m[2]] = m[3].strip()
    return out, dumps


def state(d, t):
    return dict(kv.split("=") for kv in d[t]["STATE"].split())


def steps(d, t):
    return {int(a): (int(n), int(v), c == "+") for a, n, v, c in re.findall(r"(\d+):(\d+)/(\d+)(\+?)", d[t]["STEPS"])}


def pads(d, t):
    return [int(x, 16) for x in d[t]["PADS"].split()]


def grb(c):
    return (c >> 8) & 255, (c >> 16) & 255, c & 255


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-function", "-DOBV_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(BIN),
                    str(ROOT / "firmware/src/obervave.c")], check=True)

    print("== pads: the step sequencer")
    out, d = run("steps")
    check("keys play with their velocity (C4 at 80)", "MIDI out 90 60 80" in out)
    s = steps(d, 3700)
    check("an empty pad sets its step with the pen at the pad's velocity", s.get(3) == (60, 90, False), s)
    check("a set pad tapped clears it", 3 not in steps(d, 4200), d[4200]["STEPS"])
    s = steps(d, 5200)
    check("the pen chord (three keys struck together) lands on a pad", s.get(1) == (60, 100, True), s)
    st = state(d, 6000)
    check("a held pad shows its step and the knobs edit it", st["held"] == "5" and st["view"] == "1", st)
    s = steps(d, 6300)
    check("K1 (knob 1) moved the held step's chord up, kept when let go", s.get(6) == (61, 64, True), s)
    check("the view goes back when the pad is let go", state(d, 6300)["view"] == "0", state(d, 6300))
    s = steps(d, 7000)
    check("hold pad 9 + tap pad 12: a long note 9-12", all(k in s for k in (9, 10, 11, 12)), s)
    st = state(d, 7900)
    check("SEQ + K4 (a fast turn: x3): 64 steps; > shows page 2 on the pads", st["len"] == "64" and st["seqpg"] == "1", st)
    check("a pad on page 2 sets step 17", steps(d, 7900).get(1, (0,))[0] == 60, d[7900]["STEPS"])
    check("< back to page 1", state(d, 8200)["seqpg"] == "0", state(d, 8200))
    p = pads(d, 8200)
    lit = [i for i in range(16) if p[i]]
    r, g, b = grb(p[0])
    check("set steps lit in track A's blue, empty ones dark but the beats", b > r and p[1] == 0 and p[2] == 0 and
          all(p[i] for i in (0, 4, 5, 8, 9, 10, 11)), [f"{x:06X}" for x in p])
    st, p = state(d, 8500), pads(d, 8500)
    check("K8: track B (off): no steps, the pads dark", st["trk"] == "1" and st["len"] == "0" and not any(p), (st, p))

    st = state(d, 9200)
    check("the page moved under a held pad: let go, no step stays held", st["held"] == "-1" and st["view"] == "0", st)

    print("== pads while REC is on: they play")
    out, d = run("rec")
    check("REC armed: a pad plays the pen (C4) at its velocity", "MIDI out 90 60 70" in out, out[-400:])
    check("... and lets it go", "MIDI out 80 60 0" in out)
    st = state(d, 3600)
    check("the first note started the take (REC on, running)", st["rec"] == "2" and st["run"] == "1", st)
    s = steps(d, 3600)
    check("the pen landed on step 1 as recorded, its ties keep the velocity", s.get(1, (0, 0))[:2] == (60, 70) and
          all(v[1] == 70 for v in s.values()), d[3600]["STEPS"])

    print("== faders: banks A and B, a fader acts once it moves")
    out, d = run("faders")
    st = state(d, 3400)
    check("bank A: CUTOFF RESO ENV AMT RELEASE", (st["cutoff"], st["reso"], st["envamt"], st["release"]) ==
          ("100", "50", "0", "127"), st)
    st, st0 = state(d, 3700), state(d, 3400)
    check("FADER BANK: bank B, nothing moved yet", st["bank"] == "1" and st["cho"] == st0["cho"] and st["master"] == "724",
          st)
    st = state(d, 4100)
    check("bank B: CHORUS mix, MASTER (fader 4 back down to 64)", st["cho"] == "120" and st["master"] == "515" and
          st["cutoff"] == "100", st)
    st = state(d, 4400)
    check("back to A: CUTOFF follows fader 1 again", st["bank"] == "0" and st["cutoff"] == "30", st)

    print("== wheels")
    out, d = run("wheels")
    check("at rest: no bend, no vibrato", (state(d, 3000)["bend"], state(d, 3000)["vib"]) == ("0", "0"), state(d, 3000))
    check("pitch wheel up: full bend (the MIDI scale, x the program's BEND 2 = +2 semitones)",
          state(d, 3300)["bend"] == "8191", state(d, 3300))
    check("pitch wheel down: full bend down", state(d, 3600)["bend"] == "-8192", state(d, 3600))
    check("back at rest: 0", state(d, 3900)["bend"] == "0", state(d, 3900))
    check("mod wheel up: vibrato 127", state(d, 4200)["vib"] == "127", state(d, 4200))
    check("mod wheel down: 0", state(d, 4500)["vib"] == "0", state(d, 4500))

    print("== outer keys")
    out, d = run("outer")
    check("C3 (keybed 0) plays C3 at its velocity", "MIDI out 90 48 90" in out and "MIDI out 80 48 0" in out, out[-300:])
    check("C6 (keybed 36) plays C6", "MIDI out 90 84 40" in out)
    check("OCT+ moves the outer keys too (E3 -> E4)", "MIDI out 90 64 100" in out)

    print("SMK-37 UI: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
