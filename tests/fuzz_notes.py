#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Hanging notes, hunted at random. Each scenario is a burst of panel and MIDI events on the SMK-37 host build (keys,
outer keys, MIDI notes on several channels, program changes, K3 track, K4 program, octave, MODE / ARP / VOICES by CC,
the sustain pedal, the pads, the SEQ / PARA / GLOBE / BT / FX layers held), then everything let go. The rule: with no
key and no MIDI note down, no sustain, no HOLD and the sequencer stopped, no voice may still be gated, and no sound may
still list a held key. A failing scenario is run again on its own from power-on and shrunk to the fewest events.

  fuzz_notes.py [--n 300] [--seed 1] [--keep DIR]     exit 1 when anything hangs (the repros in DIR, default build/fuzz)
"""
import argparse, os, random, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "fuzz"
BIN = OUT / "host-smk37"

# the MIDI CC of each parameter (obervave.c cc_map_init: the fixed ones, then from 20 up in table order, 64 skipped)
def cc_numbers():
    src = (ROOT / "firmware/src/obv_params.h").read_text()
    enum = re.search(r"enum \{(.*?)NPARAM", src, re.S).group(1)
    names = [n.strip() for n in re.sub(r"/\*.*?\*/", "", enum, flags=re.S).split(",") if n.strip()]
    fixed = {m[1]: int(m[2]) for m in re.finditer(r"\[(P_\w+)\]\s*=\s*\{\"[^\"]*\",[^,]+,[^,]+,\s*\w+,\s*(\d+)", src) if int(m[2])}
    used, out, nxt = set(fixed.values()), dict(fixed), 20
    for n in names:
        if n in fixed: continue
        while nxt < 120 and (nxt in used or nxt == 64): nxt += 1
        out[n] = nxt; used.add(nxt); nxt += 1
    return out

CC = cc_numbers()
TRANSPORT = False
LAYERS = {"SEQ": 15, "PARA": 13, "GLOBE": 10, "BT": 11, "FX": 14}
OUTER = [0, 1, 2, 3, 4, 32, 33, 34, 35, 36]


def scenario(rng):
    """a burst of events, (dt, line): the release pass is added by with_release, so a shrunk burst still lets go"""
    ev, t = [], 0
    for _ in range(rng.randint(8, 40)):
        t += rng.randint(5, 180)
        r = rng.random()
        if r < .22: ev.append((t, f"key {rng.randrange(27)}"))                                     # toggled
        elif r < .30: ev.append((t, f"xkey {rng.choice(OUTER)} {rng.randint(1, 127)}"))           # toggled
        elif r < .42: ev.append((t, f"mnote {rng.randrange(4)} {rng.randint(36, 84)} {rng.randint(1, 127)}"))   # toggled
        elif r < .50: ev.append((t, f"midi {0xC0 | rng.randrange(4):#x} {rng.randrange(70)} 0"))   # program change
        elif r < .56: ev.append((t, f"enc 3 {rng.choice((-2, -1, 1, 2))}"))                        # K4: program
        elif r < .62: ev.append((t, f"enc 2 {rng.choice((-1, 1))}"))                               # K3: track
        elif r < .67: ev.append((t, f"btn {rng.choice((4, 5))}"))                                  # octave
        elif r < .72: ev.append((t, f"midi 0xb0 {CC['P_MODE']} {rng.randrange(128)}"))
        elif r < .76: ev.append((t, f"midi 0xb0 {CC['P_ARP_MODE']} {rng.choice((0, 0, rng.randrange(128)))}"))
        elif r < .79: ev.append((t, f"midi 0xb0 {CC['P_UNI_N']} {rng.randrange(128)}"))
        elif r < .84: ev.append((t, f"midi 0xb0 64 {rng.choice((0, 127))}"))                     # sustain
        elif r < .90: ev.append((t, f"tpad {rng.randrange(16)} {rng.randint(1, 127)}"))           # toggled
        elif r < .95 or not TRANSPORT: ev.append((t, f"layer {rng.choice(list(LAYERS))}"))       # toggled
        else: ev.append((t, f"btn {rng.choice((16, 17))}"))                                       # PLAY / REC
    return sorted(ev, key=lambda e: e[0])


def with_release(burst, t0=3000):
    """the burst as script lines from t0, the toggles made real, then everything still down let go; -> (lines, t_end)"""
    keys, outer, midi, layers, pads, out = set(), set(), set(), set(), set(), []
    for dt, l in burst:
        t, w = t0 + dt, l.split()
        if w[0] == "key":
            keys ^= {int(w[1])}; out.append((t, f"keys {sum(1 << x for x in keys):#x}"))
        elif w[0] == "xkey":
            k = int(w[1])
            if k in outer: outer.discard(k); out.append((t, f"xkey {k} 0"))
            else: outer.add(k); out.append((t, l))
        elif w[0] == "mnote":
            ch, n = int(w[1]), int(w[2])
            if (ch, n) in midi: midi.discard((ch, n)); out.append((t, f"midi {0x80 | ch:#x} {n} 0"))
            else: midi.add((ch, n)); out.append((t, f"midi {0x90 | ch:#x} {n} {w[3]}"))
        elif w[0] == "tpad":
            k = int(w[1])
            if k in pads: pads.discard(k); out.append((t, f"pad {k} 0"))
            else: pads.add(k); out.append((t, f"pad {k} {w[2]}"))
        elif w[0] == "layer":
            if w[1] in layers: layers.discard(w[1]); out.append((t, f"btnup {LAYERS[w[1]]}"))
            else: layers.add(w[1]); out.append((t, f"btndown {LAYERS[w[1]]}"))
        else: out.append((t, l))
    t = t0 + (burst[-1][0] if burst else 0) + 350
    if keys: out.append((t, "keys 0"))
    out += [(t, f"xkey {k} 0") for k in sorted(outer)]
    out += [(t, f"midi {0x80 | ch:#x} {n} 0") for ch, n in sorted(midi)]
    out += [(t, f"pad {k} 0") for k in sorted(pads)]
    out += [(t, f"btnup {LAYERS[x]}") for x in sorted(layers)]
    out += [(t, f"midi {0xB0 | ch:#x} 64 0") for ch in range(4)]
    out.append((t + 10, "midi 0xfc 0 0"))                # (a track the GLOBE layer started: the sequencer stops)
    return sorted(out, key=lambda e: e[0]), t


def run(burst):
    lines, t = with_release(burst)
    p = OUT / "case.txt"
    text = "\n".join(f"{a} {b}" for a, b in lines) + f"\n{t + 2000} voices\n{t + 2050} end\n"
    p.write_text(text)
    out = subprocess.run([str(BIN), str(OUT / "case"), str(p)], capture_output=True, text=True, timeout=120).stdout
    m = [l for l in out.splitlines() if "VOICES" in l]
    return (m[-1].strip() if m else "no VOICES line"), text


def bad(v):
    return "gated=0 " not in v or not re.search(r"held=0,0,0,0 ", v)


def shrink(ev):
    """the fewest burst events that still hang (ddmin)"""
    fails = lambda sub: bad(run(sub)[0])
    n = 2
    while len(ev) >= 2:
        chunk = max(1, len(ev) // n); reduced = False
        for i in range(0, len(ev), chunk):
            sub = ev[:i] + ev[i + chunk:]
            if sub and fails(sub): ev, n, reduced = sub, max(n - 1, 2), True; break
        if not reduced:
            if n >= len(ev): break
            n = min(len(ev), n * 2)
    return ev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep", default=str(OUT))
    ap.add_argument("--transport", action="store_true", help="PLAY and REC too (the end stops them)")
    a = ap.parse_args()
    global TRANSPORT
    TRANSPORT = a.transport
    OUT.mkdir(parents=True, exist_ok=True)
    keep = Path(a.keep); keep.mkdir(parents=True, exist_ok=True)
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-DOBV_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="fuzz"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(BIN),
                    str(ROOT / "firmware/src/obervave.c")], check=True)
    rng = random.Random(a.seed)
    found = []
    for i in range(a.n):
        ev = scenario(rng)
        v, _ = run(ev)
        if bad(v):
            small = shrink(ev)
            v2, text = run(small)
            f = keep / f"hang-{len(found) + 1:02d}.txt"
            f.write_text(f"# fuzz_notes.py seed {a.seed} scenario {i}: {v2}\n" + text)
            found.append(f.name)
            print(f"scenario {i}: HANG ({len(ev)} events -> {len(small)}): {v2}  -> {f.name}")
    print(f"{a.n} scenarios, {len(found)} hanging")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
