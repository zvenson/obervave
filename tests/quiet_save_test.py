#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A flash erase stops the audio for ~50 ms a sector (interrupts off): it must never happen while anything is heard.
Runs tests/quiet_save.txt on the host (both boards) with OBV_SAVE_LOG=1 and checks that the output is silent
(under -54 dBFS) for 60 ms from every save, and that the saves still come once the tails have died (they are only
put off, never lost). Exit 1 on a failure."""
import os, subprocess, sys, wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"
SILENT = 64                                     # ~-54 dBFS on the 16-bit output
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + name + ("" if ok else f"  ({detail})"))
    fails += not ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for board, defs in (("fm1", []), ("smk37", ["-DBOARD_SMK37=1"])):
        bin_ = OUT / f"quiet-{board}"
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", *defs, '-DAPP_VER="test"',
                        "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(bin_),
                        str(ROOT / "firmware/src/obervave.c")], check=True)
        r = subprocess.run([str(bin_), str(OUT / f"quiet-{board}"), str(ROOT / "tests/quiet_save.txt")],
                           capture_output=True, text=True, env={**os.environ, "OBV_SAVE_LOG": "1"})
        saves = [(ln.split()[1], int(ln.split()[2])) for ln in r.stderr.splitlines() if ln.startswith("SAVE ")]
        w = wave.open(str(OUT / f"quiet-{board}.wav"))
        sr, ch = w.getframerate(), w.getnchannels()
        raw = w.readframes(w.getnframes())
        smp = memoryview(raw).cast("h")
        def peak(t_ms, dur_ms):
            a, b = int(t_ms * sr / 1000) * ch, int((t_ms + dur_ms) * sr / 1000) * ch
            return max((abs(v) for v in smp[a:b]), default=0)
        print(f"== {board}: saves at {saves}")
        loud = [(k, t, peak(t, 60)) for k, t in saves if peak(t, 60) >= SILENT]
        check(f"{board}: no save while the output is heard (60 ms from each under {SILENT})", not loud, loud)
        check(f"{board}: the settings still save once the tails have died", any(k == "SETTINGS" for k, _ in saves), saves)
        r = subprocess.run([str(bin_), str(OUT / f"quiet-max-{board}"), str(ROOT / "tests/quiet_save_max.txt")],
                           capture_output=True, text=True, env={**os.environ, "OBV_SAVE_LOG": "1"})
        saves = [(ln.split()[1], int(ln.split()[2])) for ln in r.stderr.splitlines() if ln.startswith("SAVE ")]
        w = wave.open(str(OUT / f"quiet-max-{board}.wav"))
        smp = memoryview(w.readframes(w.getnframes())).cast("h")
        print(f"== {board}, FEEDBK and SIZE at 127: saves at {saves}")
        check(f"{board}: a tail that never dies does not stop the save (within 90 s)", any(k == "SETTINGS" for k, _ in saves), saves)
        soft = [(k, t, peak(t, 60)) for k, t in saves if peak(t, 60) >= 512]
        check(f"{board}: ... and it waits until the tail is under ~-36 dBFS", not soft, soft)
    print("quiet saves: " + ("all passed" if not fails else f"{fails} FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
