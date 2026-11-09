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
    check("K5 (knob 1) moved the held step's chord up, kept when let go", s.get(6) == (61, 64, True), s)
    check("the view goes back when the pad is let go", state(d, 6300)["view"] == "0", state(d, 6300))
    s = steps(d, 7000)
    check("hold pad 9 + tap pad 12: a long note 9-12", all(k in s for k in (9, 10, 11, 12)), s)
    st = state(d, 7900)
    check("SEQ + K8 (a fast turn: x3): 64 steps; > shows page 2 on the pads", st["len"] == "64" and st["seqpg"] == "1", st)
    check("a pad on page 2 sets step 17", steps(d, 7900).get(1, (0,))[0] == 60, d[7900]["STEPS"])
    check("< back to page 1", state(d, 8200)["seqpg"] == "0", state(d, 8200))
    p = pads(d, 8200)
    lit = [i for i in range(16) if p[i]]
    r, g, b = grb(p[0])
    check("set steps lit in track A's blue, empty ones dark but the beats", b > r and p[1] == 0 and p[2] == 0 and
          all(p[i] for i in (0, 4, 5, 8, 9, 10, 11)), [f"{x:06X}" for x in p])
    st, p = state(d, 8500), pads(d, 8500)
    check("K3: track B (3.0: a bar of 16, empty): no steps, only the beats glow", st["trk"] == "1" and st["len"] == "16" and
          not any(p[i] for i in range(16) if i % 4), (st, p))

    st = state(d, 9200)
    check("the page moved under a held pad: let go, no step stays held", st["held"] == "-1" and st["view"] == "0", st)

    print("== pads: each track in its own colour, as on the screen")
    out, d = run("colors")
    hues = {"A blue": lambda r, g, b: b >= 1.3 * r and b > g, "B orange": lambda r, g, b: r >= 1.3 * g and g >= 1.3 * b,
            "C green": lambda r, g, b: g >= 1.3 * r and g >= 1.2 * b, "D pink": lambda r, g, b: r >= 1.3 * g and b >= 1.2 * g}
    for t, (name, ok) in zip((3600, 4200, 4800, 5400), hues.items()):
        st, p = state(d, t), pads(d, t)
        check(f"track {name}: its set step lights in its colour", st["trk"] == str("ABCD".index(name[0])) and p[0] and
              ok(*grb(p[0])), (st["trk"], f"{p[0]:06X}", grb(p[0])))

    print("== GLOBE / BT held: the pads show the layer and are its keys")
    out, d = run("layers")
    p = pads(d, 3200)
    hue = lambda c: grb(c)
    red = lambda c: hue(c)[0] > 2 * max(hue(c)[1], hue(c)[2], 1)
    check("GLOBE held: pads 1-4 start and 5-8 solo in the track colours, 9-12 mute dim red, 13-15 dark, 16 tap",
          all(p[i] for i in list(range(12)) + [15]) and not any(p[i] for i in (12, 13, 14)) and all(red(p[i]) for i in range(8, 12))
          and p[0] == p[4] and p[1] == p[5], [f"{x:06X}" for x in p])
    q = pads(d, 3500)
    check("GLOBE + pad 10: track B muted, its mute pad bright red", red(q[9]) and hue(q[9])[0] > 200 and hue(q[8])[0] < 60,
          [f"{x:06X}" for x in q])
    check("GLOBE + pad 6: track B soloed, its solo pad white (not its colour, not the start pad's look)",
          min(hue(q[5])) > 180 and q[5] != q[1], [f"{x:06X}" for x in q])
    q, st = pads(d, 3900), state(d, 3900)
    check("GLOBE + pad 2: track B started (the transport with it), its start pad bright in its colour",
          st["run"] == "1" and sum(hue(q[1])) > 3 * sum(hue(p[1])) and not min(hue(q[1])) > 180, (st["run"], f"{p[1]:06X}", f"{q[1]:06X}"))
    q = pads(d, 4200)
    check("GLOBE let go: the pads are the steps again", q != pads(d, 3900), [f"{x:06X}" for x in q])
    q = pads(d, 4600)
    check("BT held: pad 1 (the pattern now) bright, 2-8 dim, 9-16 dark",
          sum(hue(q[0])) > 600 and all(0 < sum(hue(q[i])) < 100 for i in range(1, 8)) and not any(q[8:]), [f"{x:06X}" for x in q])
    q = pads(d, 5000)
    check("BT + pad 3 while playing: pattern 3 is next (its pad blinks bright)", sum(hue(q[2])) > 600, [f"{x:06X}" for x in q])

    print("== faders record as the knobs do")
    out, _ = run("fader_rec")
    check("REC recording, fader A1 moved: a CUTOFF lane on track A", re.search(r"LANE A\d param=CUTOFF steps=[1-9]", out) is not None,
          [l for l in out.splitlines() if "LANE" in l])

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
