#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The whole panel at random, under AddressSanitizer + UndefinedBehaviorSanitizer (every check), both boards: every
button tapped and held, every encoder (fast turns too), keys, and on the SMK-37 the outer keys, pads, faders, wheels and
the panel check; MIDI notes, every CC, program changes, clock, start and stop. Any sanitizer report or crash fails; the
script that did it is kept (build/fuzz_ui/crash-*.txt).

  fuzz_ui.py [--n 40] [--seed 1] [--events 400]
"""
import argparse, os, random, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "fuzz_ui"
BOARDS = {"fm1": ([], 14, 7), "smk37": (["-DBOARD_SMK37=1"], 18, 8)}   # defines, buttons, encoders


def script(rng, board, n):
    _, nbtn, nenc = BOARDS[board]
    ev, t, down = [], 2800, set()
    for _ in range(n):
        t += rng.randint(1, 120)
        r = rng.random()
        if r < .18: ev.append((t, f"keys {rng.getrandbits(27):#x}" if rng.random() < .3 else f"keys {1 << rng.randrange(27):#x}"))
        elif r < .22: ev.append((t, "keys 0"))
        elif r < .34: ev.append((t, f"btn {rng.randrange(nbtn)}"))
        elif r < .40:
            b = rng.randrange(nbtn)
            ev.append((t, f"btnup {b}" if b in down else f"btndown {b}")); down ^= {b}
        elif r < .56: ev.append((t, f"enc {rng.randrange(nenc)} {rng.choice((-1, 1, -3, 3, 16, -16, 64, -64))}"))
        elif r < .66:
            k = rng.randrange(6)
            if k == 0: ev.append((t, f"midi {0x90 | rng.randrange(16):#x} {rng.randrange(128)} {rng.randrange(128)}"))
            elif k == 1: ev.append((t, f"midi {0x80 | rng.randrange(16):#x} {rng.randrange(128)} 0"))
            elif k == 2: ev.append((t, f"midi {0xB0 | rng.randrange(16):#x} {rng.randrange(128)} {rng.randrange(128)}"))
            elif k == 3: ev.append((t, f"midi {0xC0 | rng.randrange(16):#x} {rng.randrange(128)} 0"))
            elif k == 4: ev.append((t, f"midi {0xE0 | rng.randrange(16):#x} {rng.randrange(128)} {rng.randrange(128)}"))
            else: ev.append((t, f"midi {rng.choice((0xF8, 0xF8, 0xF8, 0xFA, 0xFB, 0xFC)):#x} 0 0"))
        elif board == "smk37" and r < .74: ev.append((t, f"pad {rng.randrange(16)} {rng.choice((0, rng.randint(1, 127)))}"))
        elif board == "smk37" and r < .79: ev.append((t, f"xkey {rng.choice((0, 1, 2, 3, 4, 32, 33, 34, 35, 36))} {rng.choice((0, rng.randint(1, 127)))}"))
        elif board == "smk37" and r < .84: ev.append((t, f"fader {rng.randrange(4)} {rng.randrange(128)}"))
        elif board == "smk37" and r < .87: ev.append((t, f"wheel {rng.randrange(2)} {rng.randrange(1024)}"))
        elif board == "smk37" and r < .88: ev.append((t, "check"))
        elif board == "smk37" and r < .91: ev.append((t, f"vel {rng.randint(1, 127)}"))
        else: ev.append((t, "shot"))
    ev.sort(key=lambda e: e[0])
    return "\n".join(f"{a} {b}" for a, b in ev) + f"\n{t + 1500} end\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--events", type=int, default=400)
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    rng, bad = random.Random(a.seed), 0
    for board, (defs, _, _) in BOARDS.items():
        b = OUT / f"san-{board}"
        subprocess.run([os.environ.get("CC", "cc"), "-g", "-O1", "-w", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer", "-DOBV_HOST", *defs, '-DAPP_VER="fuzz"',
                        "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(b),
                        str(ROOT / "firmware/src/obervave.c")], check=True)
        for i in range(a.n):
            p = OUT / f"case-{board}.txt"
            p.write_text(script(rng, board, a.events))
            r = subprocess.run([str(b), str(OUT / f"case-{board}"), str(p)], capture_output=True, text=True,
                               env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, timeout=600)
            err = [l for l in (r.stderr + r.stdout).splitlines() if "runtime error" in l or "AddressSanitizer" in l]
            if r.returncode or err:
                bad += 1
                keep = OUT / f"crash-{board}-{bad:02d}.txt"
                keep.write_text(p.read_text())
                where = next((l for l in (r.stderr).splitlines() if " in " in l and "firmware/" in l), "")
                print(f"{board} case {i}: exit {r.returncode}: {err[0] if err else ''} {where.strip()}  -> {keep.name}")
        for f in OUT.glob(f"case-{board}*.ppm"): f.unlink()
    print(f"{a.n} scripts a board, {bad} failing")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
