# SPDX-License-Identifier: GPL-3.0-only
# MIDI clock sync on the host: a master sends START and then 24 clocks a quarter for minutes (at tempos that are not
# a whole number of 48-frame steps, with a millisecond of jitter as USB / TRS bring), and the sequencer's beats must
# stay on the master's quarters: no drift that grows, the offset steady.
#   python3 tests/clock_sync_test.py          (from the repo root; builds its own host app into build/host)
import bisect, random, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build/host"
FS = 44100.0
START_MS = 4000          # (after the 2.4 s splash: before it the clocks would queue up and come in a burst)
# the sequencer's TRACE: a beat ('B': its number, its frame) and a step ('T': track, step * 1000000 + ..., the block's end)
TRACE = "TRACE(k,a,b)=printf(\"%c %u %u %u\\n\",(k),(unsigned)(a),(unsigned)(b),(unsigned)eng_t)"


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "obervave-clock"
    subprocess.run(["cc", "-O2", "-w", "-DOBV_HOST", "-D" + TRACE, '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"),
                    "-o", str(exe), str(ROOT / "firmware/src/obervave.c")], check=True)
    return exe


def run(exe, bpm, minutes, jitter_ms, seed, tempo_change=None):
    """the master's quarters (ms), our beats (ms) from START on, and how far (frames) track A's steps on a quarter
    (every 4th at the factory 1/16) are from our nearest beat"""
    rnd = random.Random(seed)
    lines, quarters = [f"{START_MS} midi 0xFA 0 0"], []
    t, n, end = float(START_MS), 0, START_MS + minutes * 60000
    while t < end:
        if tempo_change and t >= tempo_change[0]:
            bpm = tempo_change[1]
        if n % 24 == 0:
            quarters.append(t)
        ms = int(round(t + rnd.uniform(0, jitter_ms)))
        lines.append(f"{ms} midi 0xF8 0 0")
        t += 60000.0 / bpm / 24.0
        n += 1
    lines.append(f"{end + 200} end")
    script = OUT / f"clock_{seed}.txt"
    script.write_text("\n".join(lines) + "\n")
    out = subprocess.run([str(exe), str(OUT / "clock-test"), str(script)], capture_output=True, text=True, check=True).stdout
    beats, steps = {}, []
    for line in out.splitlines():
        k, a, b, t = (line.split() + ["", "", "", ""])[:4]
        if k == "B":
            beats.setdefault(int(a), int(b))
        elif k == "T" and a == "0" and int(b) // 1000000 % 4 == 0:
            steps.append(int(t) - 16)           # (the block's start: CTL frames before eng_t)
    bf = sorted(beats.values())
    off = [min(abs(f - g) for g in bf[max(0, bisect.bisect(bf, f) - 1):bisect.bisect(bf, f) + 1]) for f in steps]
    return quarters, [beats[i] * 1000.0 / FS for i in sorted(beats)], off


def check(name, quarters, beats, step_off, skip_s=15, max_spread_ms=10.0, max_drift_ms=3.0):
    """after skip_s: every beat within max_spread_ms of the others (the jitter and the 256-frame audio blocks), and
    the last 20 beats' mean offset within max_drift_ms of the first 20's (nothing that grows); the tracks' steps on
    the beat to the frame (they read the same quarter)"""
    n = min(len(quarters), len(beats))
    err = [beats[i] - quarters[i] for i in range(n)]
    tail = [e for q, e in zip(quarters, err) if q - START_MS > skip_s * 1000]
    spread = max(tail) - min(tail)
    drift = sum(tail[-20:]) / 20 - sum(tail[:20]) / 20
    ok = n > 100 and spread <= max_spread_ms and abs(drift) <= max_drift_ms and step_off and max(step_off) <= 16
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {n} beats, spread after {skip_s} s {spread:.1f} ms (max {max_spread_ms}), "
          f"drift {drift:+.1f} ms (max {max_drift_ms}), track A off the beat <= {max(step_off) if step_off else '-'} frames")
    return ok


def main():
    exe = build()
    ok = True
    for bpm, jit, seed in ((123.4, 1.0, 1), (97.0, 1.5, 2), (174.0, 1.0, 3)):
        ok &= check(f"{bpm} BPM, {jit} ms jitter, 5 min", *run(exe, bpm, 5, jit, seed))
    # the master's tempo moves (by less than the 0.3 % the tempo needs to be taken again, then by more)
    ok &= check("120 -> 120.3 BPM at 1 min", *run(exe, 120.0, 4, 1.0, 4, (START_MS + 60000, 120.3)))
    ok &= check("120 -> 128 BPM at 1 min", *run(exe, 120.0, 4, 1.0, 5, (START_MS + 60000, 128.0)), skip_s=75)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
