# DØG-5 — BASE LOCK & UI/UX ARCHITECTURE (decisions, 21 Oct 2026)

**Status:** decisions document, 21 Oct 2026. **Supersedes** `DOG-5-foundation.md` §1–2 and §5 (the fork facts
and the UI map), and `DOG-5-presets.md` §1 (the A/B bank). Written against the **real source**, cloned and
measured — not against release notes or README text, both of which were found to be wrong.

**Unchanged and untouched:** TONE IS KING. The character, the beam, the four scenes, the 32 SOUND briefs, the
Psych Chain design, the draw-priority mechanism, the IP rules. This document settles the *base* and the
*navigation*, nothing else.

---

## 0. DECISIONS LOG (this session)

| # | Decision | Status |
|---|---|---|
| 1 | **Base locked: OberVave `v1.5.3`, commit `7e1ccf9ca9e96a14a961e48ea8fe6efa907422df`** | LOCKED |
| 2 | Fork strategy: **pin and own**, not track upstream | LOCKED |
| 3 | HOME = PERFORM, chapter-styled; HOME always snaps back to it | LOCKED (inherited, already works) |
| 4 | Knob values appear as a **transient readout** over the art, Monologue-style; they are not permanent | LOCKED |
| 5 | Bottom bar = **preset number + preset name only** | LOCKED |
| 6 | Patch name field **widened 10 → 14 characters**; `PATCH_BYTES` 80 → 96; **`NFACT 32`**, store tag `"DOG5"`, **3 program sectors (42/sector)**. All 32 names fit | **DONE** — committed; see §5 |
| 6b | **Variable polyphony `P_POLY_N`, 1–8, default 5** | LOCKED — cheap, see §5.2 |
| 6c | Spring reverb: **"next closest thing," excellence not required** | LOCKED — closes the bank's biggest tone risk |
| 7 | **Hold HOME = live control map** (the answer to the 16 renamed controls) | LOCKED — adopted from Skiff |
| 8 | **Page families remember their last page** | LOCKED — adopted from Skiff |
| 9 | Layer-lock (tap HOME while holding a layer) | LOCKED (inherited, already works) |
| 10 | OCT− / OCT+ = octave, always | LOCKED (inherited, already works — see §4.3) |
| 11 | Moonshine's colour: purple **out**, **bubblegum magenta** in | LOCKED |
| 12 | Idle sway stays; suppressed while PRESETS is turned | LOCKED |
| 13 | Browser sim + installer page + web editor are **in scope** | LOCKED — see §7 |
| 14 | Note-event latency: **no redesign.** The base already fixes it; one coding rule carries it | CLOSED — see §3 |
| 15 | `DOG-5-design-system.md` deleted by Vincent as outdated | DONE |

---

## 1. THE BASE IS LOCKED

### 1.1 What we pin

```
Project:  zvenson/obervave
Tag:      v1.5.3
Commit:   7e1ccf9ca9e96a14a961e48ea8fe6efa907422df
Released: 2026-10-19 17:15 +0200
Licence:  GPL-3.0-only
Identity: FM-1_955
```

**`v1.5.3` is `HEAD` of `main` as of today** — verified, the two hashes are identical. So pinning here costs
nothing: we are taking the newest state of the project, not an older safe one.

### 1.2 Why this one and not another

| Tag | Date | Device-tested? | Verdict |
|---|---|---|---|
| v1.4.2 | 16 Oct | **No** — "released without a device test" | The canon's current base. Untested on hardware. Reject. |
| v1.5 | 16 Oct | "installed on the user's FM-1 and boots" | Boots only. |
| v1.5.1 | 18 Oct | — | **Not a fix.** It is 1.5 rebuilt by CI. No new code. |
| v1.5.2 | 19 Oct | "installed, USB-MIDI and a second update fine; the jack itself not tried" | Good, but superseded. |
| **v1.5.3** | **19 Oct** | **"tested on the user's FM-1, two installs"** | **Newest, and the most tested. Take it.** |

Worth noting: the previous review recommended 1.5.1 as "the newest released one." That was wrong — 1.5.1 is a
CI rebuild of 1.5 with no code change, and two better-tested releases have landed since.

### 1.3 What we gain by pinning here rather than at 1.4.2

Everything below is **inherited free** and was not in the canon's base:

- **99 program slots** — verified in source: `NPROG 99`. (The base fills 1–64 as factory; **DØG-5 cuts the
  factory bank to 32 — `NFACT 32`, so 1–32 factory, 33–99 INIT**.)
- **The note-timing fix** (§3) — the single most important inheritance.
- **TRS MIDI IN** — real hardware MIDI in via a 3.5 mm TRS-to-DIN adapter. A keyboard, drum machine or
  sequencer can drive DØG-5 live, which matters in a band.
- **The browser simulator**, in-tree and already built (§7).
- **A designed update-loader screen** — a branding surface we inherit and reskin.
- **Programs stored in their own flash sectors** (`psect`, FM-1 `0x97000`), not in the settings sector.
  (DØG-5 extends this to **3 sectors**, `0x97000–0x99FFF`, for the 96-byte record at 42 patches/sector.)

### 1.4 How to capture it — the actual procedure

GPL-3.0-only. Forking is not just permitted, it is the intended use. Obligations: keep the licence, keep the
credits (`LICENSING.md`, README — Felucca / Leo Kuroshita, SLOOP / isod89, Dinghy / zvenson), and release
DØG-5's own source under GPL-3.0-only.

```bash
# 1. Fork zvenson/obervave on GitHub (web UI) → github.com/<you>/dog-5

# 2. Clone YOUR fork and pin to the exact commit
git clone https://github.com/<you>/dog-5.git && cd dog-5
git checkout -b dog5 7e1ccf9ca9e96a14a961e48ea8fe6efa907422df

# 3. Tag the pin so it can never be lost
git tag base/obervave-v1.5.3 7e1ccf9ca9e96a14a961e48ea8fe6efa907422df
git push origin dog5 --tags

# 4. Keep upstream reachable WITHOUT tracking it
git remote add upstream https://github.com/zvenson/obervave.git
git fetch upstream --tags        # fetch only; never merge by default
```

**The rule from here: we do not merge upstream.** We cherry-pick, deliberately, only when a specific upstream
fix is wanted, and only onto a branch. Upstream shipped 1.2.1 → 1.5.3 in six days; DØG-5 rewrites the panel
and deletes the sequencer, so every merge would be a fight. The base is now ours and it stops moving.

**Also do:** `git bundle create dog5-base.bundle --all` and keep a copy off GitHub. A one-maintainer
31-star repository is not a durable archive.

### 1.5 Build-level facts that will bite if forgotten

- **Build with `-Os`, never `-O2`.** At `-O2` the FM-1's USB MIDI endpoint babbles (`urb status -75`), the
  hub disables the port, and **no installer can get another firmware back on.** Upstream lost days to this
  and the offending line was never identified. This is a brick-class trap.
- **No 64-bit division** anywhere; `tools/build.py` checks and refuses. Integers only — no FPU.
- **RAM ≤ 96 KB**, big buffers in `.pool` (274 KB of 336 KB already used by the effects).
- **Audio ISR budget:** 256 frames @ 44.1 kHz = 5.8 ms per call; house rule is to stay under half.
  Device render time shows on the GLOBAL page in µs. Budget 5800, **aim under 2900.**
- `REV_DIV 2` (half-rate reverb) exists as the first CPU lever and is **untested on hardware.**
- Rescue (OCT− held at power-on) **did not come up** when upstream's author tried it. Treat USB rescue as
  unproven and keep the second FM-1 as the real insurance.

---

## 2. MEASURED FACTS — from source, not documentation

Cloned and read at the pinned commit. **The upstream README is wrong in two places**; these numbers are right.

| Fact | Value | Where |
|---|---|---|
| Program slots | **`NPROG 99`** — base: 1–64 factory, 65–99 INIT. **DØG-5 now: `NFACT 32`, 1–32 factory, 33–99 INIT** | `obv_params.h` |
| Patch size | base **`PATCH_BYTES 80`** → **DØG-5 now: 96** (3 program sectors, 42/sector) | `obv_params.h` |
| **Patch name field** | base **`PATCH_NAME 10`** → **DØG-5 now: 14**, at fixed offset **`PATCH_NAME_AT 80`** | `obv_params.h` |
| Patch layout | base `v[0..NPARAM)` params, name trailing. **DØG-5 now: `v[0..79]` params, `v[80..93]` name, `v[94..95]` spare** | `obv_params.h` |
| Pages | **19** (`NPAGE`), as a table of `{title, {4 param ids}}` | `obv_params.h:128–150` |
| Font | **ASCII 32–126 only.** Anything else draws as `?` | `gfx.c:126–135`, `felucca_font.h:1` |
| Screen bands | `0–43` header · `46–143` page · `146–195` scope · `200–236` voice LEDs | `obv_ui.h:5–8` |
| Scope capture | `scope[SCOPE_N]`, armed by main loop, **triggers on a rising zero crossing**, stride from the last note's period, refreshed every 50 ms | `obv_engine.h:74,701–706` |
| Transient message | `say(s)` → `toast` + `toast_t` timestamp | `obervave.c:133` |

### 2.1 ⚠️ The font cannot draw Ø

`THE DØG` currently renders as **`THE D?G`** on the panel. The font is cut to ASCII 32–126 and `glyph()`
replaces everything outside that range with `?`. This affects the patch name, the boot screen and any
on-screen `DØG-5` or `DØGS` wordmark — and the slashed O is a hard brand rule.

**Fix, and it is cheap:** the font is generated by `tools/gen_font.py` from `ter-u16n.bdf` (Terminus), which
**does** carry Ø in its Latin-1 range. Either extend the generated range past 126, or map Ø into an unused
low slot. A generator change, not a drawing job. **Do it early** — it is the kind of thing that gets found
in a venue otherwise.

### 2.2 The bark already has an honest data feed

The scope is not something DØG-5 has to invent. It is already captured in the audio engine, **triggered on a
rising zero crossing with the stride taken from the last note's period** — which is exactly what makes a
waveform sit still on screen instead of crawling. The beam's truthfulness is already engineered. DØG-5
re-draws that buffer; it does not re-measure the signal.

---

## 3. NOTE-EVENT LATENCY — CLOSED, and Vincent was right

The previous review flagged note timing as a hole in rule 1 and asked for a measured latency ceiling. Having
read the source: **Vincent's instinct was correct, and the reason is better than instinct.**

Upstream hit this exact problem on 16 Oct (a user video titled *"THIS FIRMWARE NEEDS A FIX"*: notes stumbled
under CC streams, chords came in staggered) and fixed it **architecturally** in 1.4.2:

> the keys and MIDI are `keys_in` / `midi_in`; the main loop calls them once a pass, and **`app_idle` (notes,
> CC, clock only) runs them while `lcd.c` waits for a strip's DMA**, and between `ui_page`'s knobs, only while
> `idle_ok`. A note redraws the page only where it shows.

So the screen's DMA wait is no longer dead time — notes are serviced *inside* it. The mechanism I guessed was
right; the fix already exists; and **we inherit it the moment we pin 1.5.3.**

**What this costs us going forward: one rule, not a redesign.**

> **DRAW RULE:** every DØG-5 draw routine that waits — on DMA, on a fill, on a long blit — must call
> `app_idle()` in its wait loop. No new drawing code is allowed to block without yielding to notes.

That rule goes into the canon beside the draw-priority list. It is the whole of the mitigation. No latency
ceiling needs measuring before the build, and no art gets cut for it. If a symptom ever appears on the device,
the cause will be a draw routine that broke this one rule, and that is a one-line fix.

**Tone is king, and in this case the base already paid for it.**

---

## 4. THE SCREEN ARCHITECTURE

### 4.1 ⚠️ A conflict in the brief, and how it resolves

Two instructions this session pull against each other:

1. "Follow your lean — option (a)." Option (a) was *the bottom bar carries the four PERFORM values permanently.*
2. "All I want on the bottom bar is the preset number and the preset name," Monologue-style.

Those cannot both be true. **Resolved in favour of (2), the Monologue reference** — because (2) is the more
specific instruction, it is the one tied to hardware Vincent actually plays, and it protects the art.

The resolution is neither (a) nor (b). It is **the Monologue's own pattern**, and the base already has the
mechanism for it:

> **The HOME screen is the dog, the beam, the field and the bar. The four PERFORM values are not drawn
> permanently. Turning a knob pops its value as a transient readout over the art; it decays and the art
> returns.**

That is exactly what the Monologue does with its oscilloscope — the scope is the resting state, touching a
control pops the value, the value goes away. It is what Vincent is already used to reading in the dark. And
`say()` / `toast` already implements the timed transient message, so this costs approximately nothing.

The art never gets permanently eaten, the live values are always one touch away, and the bar stays sacred
carrying only the two things Vincent asked for.

### 4.2 The screen inventory

**HOME is PERFORM.** Confirmed in source (`obervave.c:1592–1595`): `else np = PG_PERFORM;`. The base already
behaves exactly as Vincent described — HOME snaps to the live performance page, from anywhere. Nothing to
build. `PG_PERFORM` is also the power-on page, and the **HOME button's LED lights whenever you are on it**
(`obervave.c:1795`), so the panel itself tells you where you are without looking at the screen. Keep that.

Pages are a **table**, not code. Each row is `{title, {four parameter ids}}`. Adding, removing, renaming or
reordering a screen is a table edit. "However many screens we need" is therefore a cheap question, and the
answer is 19 — one less than the base, because the sequencer goes and the Psych Chain adds one:

| Family | Button | Pages | Count |
|---|---|---|---|
| **LIVE** | HOME | **HOME** (the dog, the beam, the field, the bar) | 1 |
| **Voice** | EDIT · *DIG* | OSC 1 · OSC 2 · OSC MOD · FILTER · FILTER + | 5 |
| **Envelopes** | ENV · *HEEL* | FILTER ENV · AMP ENV | 2 |
| **Modulation** | LFO · *ZOOMIES* | LFO · LFO DEST · LFO + | 3 |
| **Psych Chain** | FX · *FETCH* | DRIVE · BBD DELAY · SPRING · TAPE | 4 |
| **Arp** | ARP · *BARKS* | ARP | 1 |
| **System** | GLO · *FLEAS* | GLOBAL · VOICE · OUTPUT | 3 |
| | | **TOTAL** | **19** |

Four Psych Chain pages instead of the base's three effect pages: DRIVE and TAPE are new, CHORUS is retired,
DELAY and REVERB are re-voiced. The freed `P_CHO_*` parameter slots carry Drive and Tape, as already planned —
no new patch storage.

The sequencer (`PG_SEQ`, five views) is **deleted**. That is a large, well-built subsystem being thrown away,
and it is the right call: it is features, and DØG-5 trades features for tone and simplicity.

### 4.3 Correction: nothing was being lost on OCT

The previous review implied the UI map was silently losing the octave buttons. **That was wrong, and the
confusion is worth clearing up properly.**

In the base, `OCT−` / `OCT+` **always move the octave** — range −2 to +2, and pressing both returns to 0, with
`"OCTAVE 0"` shown on screen. Exactly as on any synth, exactly as Vincent wants.

Undo/redo only happens when **EDIT is being held as a layer** — `EDIT (held) + OCT−/OCT+`. It is a sequencer
feature for undoing a pattern edit. Since DØG-5 deletes the sequencer, **undo/redo goes with it**, and the
octave behaviour is untouched. Nothing is lost.

### 4.4 Adopted from Skiff — the best navigation model in the scene

Skiff (illuminus5, 1.3.2, 50★) is the strongest reference. Upstream already borrowed its update-loader idea
and credited it. Three patterns to take:

**(i) ⭐ Hold HOME = a live control map.** On Skiff, holding HOME shows a map of what every button, encoder
and key does *on the current page, right now*.

**This is the answer to the sixteen renamed controls, and it is better than the legend card the review
proposed.** The problem was that the panel is screen-printed `FX` while the screen says `FETCH`, so the player
has to translate in the dark. With this, there is no translation and no memorisation: hold the one button you
already know, see every dog name in its real panel position, let go. It is contextual, it is per-page, it
costs no permanent screen space, and it means a first-time user can learn the whole instrument without a
manual.

It also lets the renames stay **pure** — no dual-labelling in the headers, no `FETCH · FX` compromise. The
header shows the dog name alone, because the map is one hold away. The joke survives completely intact.

**(ii) Page families remember their last page.** On Skiff, "every family reopens the page you left." The base
does *not* do this — `cycle_page(PG_OSC1, 5)` always restarts the cycle from a fixed page. So if you are
deep on `FILTER +`, go HOME to play, then press EDIT, the base dumps you back at `OSC 1` and you have to tap
four more times to get back. **With families-remember, you land on `FILTER +` again.** For a live instrument
this is a large, cheap win: four taps saved, in the dark, mid-song. One `uint8_t` per family.

**(iii) Each page draws what its knobs affect** — the envelope shape, the LFO wave, the delay's echoes, the
reverb's tail. This is already DØG-5 law (*"the screen teaches the instrument"*), and it is where the
per-chapter visual styling lives on the parameter pages, so they are not plain text screens.

**Not adopted:** Skiff uses `OCT+` to confirm and `OCT−` to go back in dialogs. That collides with decision
10 — OCT is always octave. Rejected.

### 4.5 The bottom bar

Preset number and preset name. Nothing else, ever.

- Format: `17 NORTH WATER` — number, space, name. No chapter letter (the number already encodes the chapter,
  and the field tells you the scene).
- Name field: **10 characters**, hard (§2).
- Type size: the base's `FONT_S` is Terminus `ter-u16n` — a 16-pixel bitmap face, which is the scene's de
  facto standard for exactly this and is legible at a metre. Use it at its native size; do not shrink it.
- The bar is the one element that never degrades under load and is never broken by the fracture. Top breaks,
  bottom is sacred. Unchanged.

### 4.6 Confirmed behaviours, inherited free

- **Layer-lock:** hold a function button, tap HOME, the layer locks open and the screen says
  `LOCKED: ITS BUTTON ENDS IT`. Its own button — or ENV / LFO / ARP — releases it. Already implemented
  (`obervave.c:1592–1599`). Adopted as decision 9.
- **Page button LEDs light for the page you are on**, and the key LEDs show sounding notes. Panel-level
  state without reading the screen. Keep.
- **Idle sway:** stays, as instructed. Suppressed on note-on *and* while PRESETS is being turned.

---

## 5. THE PRESET NAMES — RESOLVED

**The field is widened from 10 to 14 characters.** At 14, nothing in the bank is over, and only four names
were changed by choice:

| # | Was | Now | Len |
|---|---|---|---|
| 09 | Skinhead Bubble | **Skeenhead** | 9 |
| 17 | North Water Ice | **Titanic** | 7 |
| 18 | Rotoscoped Void | **Roto-scopo** | 10 |
| 19 | Blackwater Tank | **Blackwater** | 10 |

The nine others that exceeded the old 10-character field are kept untouched: Cement Grind (12) · Tape Splice
(11) · Atlantic Swell (14) · Slapback Strut (14) · Kelp Tangle (11) · Ghost Board (11) · Cowl Shadow (11) ·
Melting Orb (11) · Fugue State (11).

**Longest in the bank: Atlantic Swell and Slapback Strut, both 14.** 14 is therefore exactly the right number
— no slack, no waste.

**The bar is comfortable.** `FONT_S` advances 8 px per character on a 240 px line, so the worst case —
`11 Atlantic Swell` — occupies 136 px, leaving 104 px spare. Legibility was never the constraint; the patch
struct was.

Spaces count. Lower case is safe (the font folds it to upper case). ASCII 32–126 only.

**30 THE DØG** fits at 7 characters but **needs the font fix in §2.1**, or it shows as `THE D?G`.

### 5.1 ✅ DONE — grew the struct in the same change: `PATCH_BYTES` 80 → 96

Measured: `NPARAM` 63 + name 10 = 73 of 80, so 7 bytes spare in the base. Widening the name to 14 fits
(63 + 14 = 77, 3 spare) — but DØG-5 adds its own parameters (Tape's second macro, `P_POLY_N`), which would
land the struct at **79 of 80: one spare byte.**

A v1.1 is already planned off the back of hardware testing, and a v1.1 with one spare byte cannot add a
single parameter without migrating every saved patch — so we went to 96 **before any patch existed** (free
migration).

**Final 96-byte layout** (committed): `v[0..79]` parameters (`NPARAM 63`, so params occupy `0..62` and
`63..79` is a 17-byte growth gap), name at the fixed offset **`PATCH_NAME_AT 80`** spanning `v[80..93]`
(14 chars), and `v[94..95]` spare. Headroom for v1.1: the 17-byte parameter gap plus 2 trailing bytes.
It cost **1,584 bytes of flash** (16 × 99 slots) against a preset budget that was ~5 KB of 425 KB. The store
carries a new **`"DOG5"` version tag** so old 80-byte data is never misread, and the 99 slots now span
**3 program sectors at 42 patches each** (proven by the host self-test: 99 distinct patches round-trip
byte-exact across a power cycle, one save touches only its own sector, an old 80-byte `"BOVP"` sector loads
as empty INIT).

### 5.2 Variable polyphony — `P_POLY_N`, 1–8, default 5

Verified in source: every path that allocates or counts voices takes its window from one function,
`part_cards(part, &lo, &n)` — `voice_alloc`, `uni_count` and `voice_trigger` all read it. So the whole
feature is **one clamp in one function plus one parameter.**

It will **not** make the synth faster in normal playing — the render loop already skips idle voices, so an
unused voice costs essentially nothing. What it buys instead:

- a guaranteed **worst-case CPU ceiling**, which matters while the art competes for the same milliseconds;
- genuine **voice-stealing character** — a 5-voice poly steals notes, an 8-voice one mostly doesn't, and
  that is a tonal difference, not a spec one;
- it **settles the open 5-vs-8 question by ear, on the device, with a knob** — which is the only way that
  question should have been answered.

Ships at 5. The five-motif is intact; 6, 7 and 8 are there when a patch wants them. **This closes open
decision 3.**

*Consequence:* the VOICE page's four slots are full (MODE, VOICES, DETUNE, GLIDE), so `P_POLY_N` needs a
fifth. VOICE becomes two pages and the screen count goes **19 → 20**. It is a table row.

### 5.3 The spring — "next closest thing," and that closes the bank's biggest tone risk

Vincent's instruction: re-shape and re-voice toward spring, excellence not expected, real analogue units
exist for the real thing.

The re-voice: push the plate's two swaying allpasses **faster and shallower** for the chirp, short
`PRE-DLY`, brighter `DAMP`, and clamp `SIZE` so it cannot bloom into a hall. It lands as a short, bright,
boingy reverb that behaves like a spring — and that is explicitly enough.

**Why this is bigger than one effect:** Bee-tch is 7 of 8 patches dependent on the spring, so an unresolved
spring left a quarter of the bank unresolved. Declaring it good-enough in advance means Chapter II is voiced
on the host build like every other chapter, with no gate in front of it. The build review named this the
single largest tone bet in the project; it is now not one.

---

## 6. MOONSHINE'S COLOUR — purple out, bubblegum magenta in

Purple was always the weak one: on a small backlit panel behind plastic it muddies, and it is the hue the
panel's RGB565 handles worst. Dropped, as instructed.

**Replacement: hot bubblegum magenta**, provisionally `#FF2FA0`, final value eyeballed on hardware.

Why this and not the alternatives:

- **It is the furthest hue from the other three.** Acid green, cyan and amber occupy the green-through-red
  arc. Magenta is the one strong hue none of them is near, so the four scenes separate at a glance.
- **It is bright by construction.** Magenta is the panel's red and blue channels both at full — after white,
  the most luminous colour available in RGB565. It cannot muddy the way purple does, because purple is
  magenta with the brightness taken out.
- **It is more on-brand than purple ever was.** The band plays *bubblegum* egg punk. A hot bubblegum moon is
  a better egg-punk idea than bruised purple smog, and it feeds the gross-cute juxtaposition directly: the
  ugliest dog in the world howling at a sweet pink moon.
- It sits fine against the Moonshine figure inversion — pink field, near-white dog, no collision.

The scene's written character (fog, weight, the slow drone chapter) does not change. Only the colour, and it
gets louder, which suits the chapter's contrast better than a muted purple did.

---

## 7. SIM, INSTALLER AND EDITOR — now in scope

The scene has standardised on all three. In scope, as instructed. Honest assessment of what each costs:

### 7.1 Browser simulator — INHERITED, nearly free ✅

Already in the base, already built:

```
web/sim/build.sh     the build (zig cc + wasm-opt)
web/sim/sim.c        the harness
web/sim/index.html   the page: screen, keys, buttons, knobs
web/sim/sim.js       panel wiring
web/sim/worklet.js   audio via AudioWorklet
docs/sim/            the built output, obervave.wasm already committed
node tests/sim_test.mjs
```

It is the **same C sources** compiled to WebAssembly, so the sim is the firmware, not a mock-up. Work
required: rebuild it as DØG-5 and reskin the page. That is it.

**This changes how the art gets built.** The full screen — dog, beam, field, bar — can be drawn, iterated and
judged in a browser at real 240×240, with real audio driving the real scope buffer, with no hardware and no
flash cycles. It is a better development loop than the hardware would have been, and it is available now.

### 7.2 Installer page — mostly free ✅

Two routes, take both:

1. **The Firmware Hub.** Release DØG-5 as a GPL-3.0 `.fwsc` on GitHub and mail `zven@thefirmwarehub.com`.
   The hub mirrors it, follows every release automatically, and gives one-click install in Chrome or Edge —
   plus a "Built from source" provenance key if it is built in GitHub Actions. No work beyond releasing.
2. **An own page**, which is what most projects do: a short page wrapping the same Web MIDI install flow,
   carrying the art. The pattern is everywhere in the scene to copy from.

Also inherited: **the update-loader screen**, redesigned upstream in 1.5.3
(`firmware/loader/ldr_screen.c`) — name, status, twenty segments with a running light, the percentage. It is
already a designed surface; DØG-5 reskins it rather than building it. First impression on every machine the
firmware ever lands on.

### 7.3 Web editor — ⚠️ the one that is NOT free

OberVave has **no** web editor. Felucca, SLOOP, Skiff, Melodee, JIANT and GammaWave all do, so it is the
expected third piece, but it is genuine new work: a page that talks SysEx/MIDI to the device, reads and
writes the 80-byte patch struct, and presents 64-odd parameters.

**Recommendation: ship without it.** It is the least important of the three for this instrument. DØG-5 is a
live performance synth, not a sound-design workstation — the editing happens on the panel, in the dark, with
four knobs, which is the whole design. The sim and the installer carry the release; the editor can follow if
anyone asks for it. If it does get built, build it against the parameter table so it generates itself.

---

## 8. WHAT THIS MEANS FOR THE SCHEDULE — honestly

The stated goal is to finish the build, with the sim and the pages, before the FM-1 arrives.

**Achievable before it arrives:**

- Forking, pinning, tagging the base. An hour.
- Getting the host build and the sim running as DØG-5. An evening.
- The font fix for Ø.
- The page table: 19 screens, renamed, families-remember, hold-HOME map.
- The HOME screen architecture and the transient readout.
- The bottom bar.
- The whole art layer, iterated in the browser at real resolution.
- Deleting the sequencer.
- The 13 renamed presets.

**Not achievable before it arrives, and should not be rushed:**

- **The Psych Chain.** Drive and Tape are two new effects written in fixed-point integer DSP with no FPU,
  and Delay and Spring are re-voices that have to be judged by ear. This is the tone work, and tone is king,
  so it takes the time it takes.
- **Voicing 32 patches by ear.** Half the bank (17 of 32) can't even be finished until the Psych Chain
  exists. There is no version of this that happens quickly and is also good.
- **Anything that needs the real panel.** The four spot colours, the legibility floor, the dither, the CPU
  and render-time readings. The canon has always said the final colour values get eyeballed on hardware.

**The honest framing:** what finishes before the FM-1 lands is **the instrument's skeleton and its skin** —
base, navigation, screens, art, sim, pages. What waits for the hardware is **the voice**. That is the correct
order anyway: tone is king means the tone gets judged last and longest, on the real thing, not rushed to meet
a delivery date.

Two tone jobs *can* start now on the host build without hardware, and should, because both de-risk the bank:
**the spring re-voice** (Bee-tch is 7 of 8 patches dependent on it) and **the voice count by ear**, 5 vs 8.

---

## 9. CANON REWRITE — what this invalidates

| Doc | Action |
|---|---|
| `DOG-5-design-system.md` | **Deleted by Vincent.** Rebuild later from the character brief, not from the old one. |
| `DOG-5-foundation.md` | §1–2 rewrite to the pinned base and §2's measured numbers. §5's UI map **replaced** by §4 here. Add the DRAW RULE (§3) beside the draw-priority list. Add the `-Os` trap and the font limit. |
| `DOG-5-presets.md` | §1 replaced: 99 slots, not a 64-slot expansion; the remaining work is a **write-lock — lock slots 1–32 (THE POUND, factory, write-protected), 33–99 live (THE PACK)** — not a store expansion. The factory bank is now 32 patches (`NFACT 32`). §3 names updated once Vincent returns the 13. Open decision "64-slot store" closes. |
| `DOG-5-chapter-specs.md` | Mechanically sound; re-skin Moonshine from purple to magenta, and re-skin the scene prose from orb/reaper to the character brief's world. |
| `DOG-5-art-direction.md` | Retire §1–2 and §4–5 (superseded by the character brief). §3 and §6–10 survive. Add the parameter-page styling note from §4.4(iii). |
| `DOG-5-character-brief.md` | Current. Add the screen architecture (§4) and the magenta change. |

**Order:** foundation first (it is the one the build reads), then presets once the names come back, then the
scene docs.

---

## 10. OPEN — needs Vincent

Decisions 1–3 of the previous revision are **closed**: the names are settled (§5), the field is widened to 14
with the struct going to 96 (§5.1), and the voice count becomes a knob rather than a choice (§5.2).

What remains is in `DOG-5-build-kit.md` §3 — the fork on GitHub, the credit line, the hand-drawn art, and
four quick confirmations. The only item carried over from here:

1. **When does the FM-1 actually land?** Currently "within two weeks." It sets how much of Stage 3/4 in the
   build kit has to be finished before it arrives, and how much can follow.
