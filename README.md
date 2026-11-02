<p align="center"><img src="assets/logo.png" width="800" alt="OberVave: the name in red glowing outlines, three blue bands through the lower half of the letters"></p>

# OberVave

**An 8-voice analog-style polysynth for the M-VAVE FM-1, on four tracks: a mixer, effects, a master, a step sequencer.**

OberVave turns the FM-1 into a deep instrument in the spirit of the classic American 8-voice poly synths of 1979-84:
two oscillators a voice with hard sync and cross-modulation, a 12 / 24 dB filter with an SEM-style
lowpass-to-highpass morph, two envelopes, an LFO and an arpeggiator. Since 3.0 it plays **four tracks**, each with its
own sound, arpeggiator and sequence, through a **mixer** with chorus, delay, plate reverb and drive as send effects
and a **MASTER** (DJ filter, drive, compressor). The tracks start and stop on the bar, like clips; every pattern keeps
its sounds and its mix. 99 programs (70 factory sounds).

<p align="center"><img src="docs/screen.png" width="240" alt="OberVave's screen: program 01 DARK ARP, the ARP page with four knobs, the scope, eight voice LEDs"></p>

> **3.3.2** (identity `FM-1_9533`): OberVave's screen, live and as big as you like, on a computer or phone with
> [FM-1 Mirror](https://github.com/pfkellogg/fm1-mirror) by Patrick F. Kellogg – for players who cannot read the 1.54-inch
> display. OberVave sends its screen only while FM-1 Mirror is open and asks for it; playing is unchanged.
>
> **3.3.1** (identity `FM-1_9533`): newer FM-1s from AliExpress carry another
> flash chip (UC25HQ80, JEDEC `B3 60 14`) – OberVave saved to RAM only on them and refused updates; 3.3.1 takes it as the
> Puya one (thanks to Paul Nederhoed for finding it). Nothing else changed.
>
> **3.3** (identity `FM-1_9533`; on the Firmware Hub, built and signed by this
> repository's workflow). 3.3 is fatter: **LP 24 is a real four-pole ladder** now, four stages with the resonance fed back
> around all four as in the filter chip of the later eight-voice synths, a tanh in the loop, so the resonance compresses
> the sound instead of thinning it and the bass stays. **No hard clipping anywhere in a voice**: the oscillator mix and the
> filter's output go through soft stages instead of clamps; DRIVE sits in front of that input stage, from warm to fuzz
> (it was a fuzz at half level from the first notch). VINTAGE moves the cutoff too, slowly, each voice on its own. The
> levels stay within a dB of 3.0. VOICE (mode, unison, detune, glide) is under EDIT after OSC 2 now; GLO is GLOBAL /
> OUTPUT. The same engine is in the native VIBE firmware 3.3. Build it yourself: `./build.sh`, then
> `tools/fm1_install.py build/obervave.fwsc`.
>
> Before 3.3: 3.0 brought OberVave's VIBE edition back to the FM-1 and the SMK-37 (four tracks on the eight voices, the
> mixer, DRIVE and MASTER, track launch on the bar, patterns with their own sounds and mix, recording with a quantise per
> track, legato, arp takes, recorded knob moves, SYNC AUTO / INTERNAL; 3.0.1 gave it its own identity `FM-1_9530`); 1.5.3
> showed the update loader's progress in the Firmware Hub's look (the idea from Skiff); 1.5.2
> opened the FM-1's **TRS MIDI IN** jack (through a TRS-to-DIN adapter, type A or B); 1.5 brought 99 program slots
> and "save as"; 1.4 a second sound on track B; 1.3 the full-screen sequencer as SLOOP's step page. Do not install
> 1.1: its USB-MIDI out is broken (1.2 fixed it).

**Play it in your browser:** `docs/sim/` is the firmware itself (the same C sources, compiled to WebAssembly) with
the FM-1's screen, keys, buttons and knobs, your computer keyboard or a MIDI keyboard. Build it with
`sh web/sim/build.sh`, try it with `python3 -m http.server -d docs/sim` (see `web/sim/build.sh` for the two tools).

**M-Vave SMK-37:** see [SMK-37 instructions](SMK37.md).

Hear it: [the video](video/) is nine OberVaves (the host build) playing one slow, dark track; every sound in it,
drums included, is this firmware.

## The synth

| | |
| --- | --- |
| **Voices** | 8, shared by the four tracks' sounds (as the originals' eight voice cards). POLY (round-robin, steals the oldest released), DUAL (2 detuned voices a note), UNISON (2-8 voices, detune and pan spread), MONO (last note, legato); a MONO or UNISON sound keeps its voices while the others play chords. Glide in every mode. |
| **Oscillators** | 2 a voice: saw, pulse, saw+pulse, off. Pulse width and PWM. OSC 2: ±24 semitones, fine detune, hard sync to OSC 1, filter envelope to pitch (sync sweeps), X-MOD (OSC 2 → OSC 1 linear FM). Noise. PolyBLEP anti-aliasing, free-running phases. |
| **Filter** | State-variable, trapezoidal: LP 12, HP 12, BP 12, notch, LP 24 (two cascaded), SEM (LP → notch → HP by MORPH). Cutoff, resonance with a soft, bounded self-oscillation, envelope amount (bipolar), key tracking, velocity, drive before the filter. |
| **Envelopes** | Filter ADSR and amp ADSR with analog RC curves, 0.5 ms to 10 s. |
| **LFO** | Triangle, saw, ramp, square, S&H, smooth random; 0.05 to 30 Hz; delay / fade-in; phase SYNC / FREE / SPREAD (an offset per voice). To pitch (OSC 1 / 2 / both), PWM, filter, amp. The mod wheel adds vibrato and filter. |
| **Vintage** | Each of the 8 voice cards has its own small tune, cutoff and envelope offsets, plus a slow random walk on pitch, all scaled by one knob. SPREAD pans the cards like the originals' pan pots. |
| **Effects** | Send effects, fed by each track's sends in the mixer (the song's, not a sound's: a program change never cuts a reverb tail): chorus (two taps, Hermite-interpolated), ping-pong delay in note values of the tempo (1/16 to 1/2, dotted and triplet, the time slews like a tape), plate reverb (Dattorro's figure-of-eight tank, full sample rate), drive (OVERDRIVE, DIST, FUZZ, CRUSH; tone, return). Then the **MASTER**: volume, a DJ filter (one knob: low-pass below the middle, high-pass above), drive, a compressor, and a soft limiter that never lets it clip. |
| **Arpeggiator** | UP, DOWN, UP/DN, RANDOM, AS PLAYED; 1-4 octaves; 1/4 to 1/32 with triplets; gate; latch (HOLD). Sample-exact, in the audio interrupt. Follows MIDI clock. |
| **Sequencer** | Four tracks of 256 steps each. A step is a chord of up to 4 notes with velocity, gate or a tie into the next step, a ratchet (1-4 hits), a chance and a nudge (how late in the step it sounds). Each track has its own length (polymeters), note value (1/4 to 1/32 with triplets), swing and transpose, and up to four recorded knobs. The tracks start and stop on the bar. Step recording on its own full screen and live recording into the running loop, quantized per track (1/32 .. 1/4, or OFF: as played). 8 patterns in flash, each with its four sounds and its mix, switched on the bar while it plays, saved by themselves. Sample-exact in the audio interrupt; follows MIDI clock, start and stop (or ignores them: SYNC INTERNAL). |
| **Programs** | 99: 70 factory sounds (01 DARK ARP, 26 UPSIDE ARP, 29 NEON PAD the dark ones; brass, strings, leads, basses, bells, pads; 65-70 KICK, SNARE, HIHAT, CLAP, E-PIANO, WURLI), 71-99 INIT; save anywhere. Hold SAVE and press the SELECT knob for a new one at random, within useful ranges. |
| **MIDI** | USB-MIDI in and out; the FM-1's TRS MIDI IN jack (3.5 mm, through a TRS-to-DIN adapter, type A or B). Notes, velocity, pitch bend (range 0-24), program change, sustain, mod wheel, clock / start / continue / stop. OMNI plays the chosen track; with a MIDI channel set, that channel is track A and the next three are B-D (notes, CC, program change); the keys send on the chosen track's channel. CC 74 cutoff, 71 resonance, 73 / 72 amp attack / release, 5 glide, 1 mod depth, 7 volume, 64 sustain, 120 / 123 all off; every other parameter from CC 20 up in the order of the parameter table (see `firmware/src/obv_params.h` and CLAUDE.md). |

Integers only: the chip has no FPU. The whole engine is fixed point, the tables are generated by
`tools/gen_tables.py` and committed.

## The panel

The panel works as sloopDX's: **tap** a function button (press and let go, touching nothing else) and its pages
open; **hold** it and it is a **layer**: the white keys and the knobs change job until you let go. **HOME** tapped
while a layer is held locks it open; its button lets it go.

| Hold | Keys | KNOB 1 · 2 · 3 · 4 | Tap |
| --- | --- | --- | --- |
| **GLO** | white 1-4 (F3 .. B3): start / stop track A-D on the bar (REC lit: record it); 5-8: mute A-D; 9-12: solo A-D; G5: tap tempo | - | GLOBAL (TEMPO, CLICK, MIDI CH, TUNE; the CPU time and the sync in its title) / OUTPUT (VOICE is under EDIT since 3.3, after OSC 2) |
| **SEQ** | steps 1-16 of the page (tap: set with the last note or chord / clear; hold + a later step: one long note); black keys F#3 .. A#4: page 1-8 | NOTE · DIV · SWING · LENGTH; a step held: NOTE · LEVEL · RATCHET · GATE | the sequencer (full screen); again: its next view |
| **EDIT** | erase that note (playing: as the playhead passes) | SHIFT · LENGTH x2 / 1/2 · TRANSPOSE | OSC / FILTER pages; in STEPS: clear the step |
| **SAVE** | white 1-8: go to pattern 1-8; black 1-8 (F#3 .. A#4): copy this pattern there (with SELECT pressed: a random sound) | - | twice: save the track's program (PRESETS in between: into another slot); in the sequencer: SAVE PATTERN?, again: saved |
| **FX** | notes | CHORUS · DELAY · REVERB mix · DELAY TIME | CHORUS / DELAY / REVERB / DRIVE |

- **ALGORITHM** picks the track (A-D, in its colour: blue, orange, green, pink): the keys play its sound, every page
  edits it, **PRESETS** picks its program. **SELECT** turned: in STEPS a bar on, in the sequencer's other views and
  the MIXER the next pattern (on the bar), elsewhere the tempo; **SELECT held** in STEPS shows STEP+, pressed on
  GLOBAL it switches SYNC (AUTO: a MIDI clock leads; INTERNAL: ignored). **OCT- / OCT+** the octave (both: 0).
- **HOME** PERFORM (CUTOFF, RESO, ENV AMT, RELEASE); again: the **MIXER**; there, again: its next row (LEVEL, PAN,
  SEND CHORUS, SEND DELAY, SEND REVERB, SEND DRIVE, MASTER). KNOB 1-4 are tracks A-D in the row (MASTER: VOLUME, DJ
  FILTER, DRIVE, COMP); under them each track's level, a meter, PLAY / STOP and its mute and solo.
- **PLAY** starts the tracks that are on from step 1 / stops (the light flashes on the beat). **REC** playing:
  record the chosen track now / stop; stopped: armed, the first note starts (CLICK on COUNT-IN: PLAY counts in a
  bar first). **REC held 1.2 s** clears the track (its recorded knobs too). Turning a knob while REC records writes
  its value onto the steps (four knobs a track). **EDIT + OCT- / OCT+** undo / redo.
- **ENV**, **LFO** their pages; **ARP** the arp page, on it the ARP button is HOLD (latch, each track its own).
- The key LEDs show the notes that sound, the page buttons light for the page you are on, the eight round LEDs on
  the screen glow with each voice's envelope.

## The sequencer

**SEQ tapped** gives the sequencer the whole screen, as SLOOP's step page. SEQ tapped again steps through its views
(**TRACKS**, **STEPS**, **TRACK**, **PATTERNS**), **HOME** leaves.

**TRACKS** shows A-D one under the other: each with its program, its state (PLAY, STOP, NEXT / ENDS while it waits for
the bar, MUTE), its 16 steps with the playhead and, for a track longer than a bar, a line with a piece per bar filled
up to where it plays (and `2/4`: bar 2 of 4). New patterns give every track a bar of sixteenths; LENGTH 0 switches a
track off.

| View | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **TRACKS** (PRESETS: the track's sound) | CUTOFF | LEVEL (the mixer's) | LENGTH | QUANT (REC's grid: 1/32 .. 1/4, OFF) |
| **STEPS** (SELECT held: STEP+) | the step cursor | NOTE (the chord moves) | GATE (to TIE) | VELOCITY |
| **STEP+** | the step cursor | RATCHET 1-4 | CHANCE | the step ON / OFF |
| **TRACK** | LENGTH (fast: by 16; up to 256) | DIV | SWING | TRANSPOSE |
| **PATTERNS** | the slot | - | GO there | COPY this one there (twice on a used one) |

- **Step entry** (STEPS): a key or a chord goes into the cursor's step, and the cursor moves on when you let go; a
  key held and KNOB 1 turned on makes a long note. **EDIT tapped** clears the step. MIDI notes write too. When the
  playhead is in another bar than the one shown, the info line says `PLAYS 2`.
- **Recording:** REC records live on every page, onto the chosen track, on its grid (QUANT) or as played (OFF: a note
  sounds as late in its step as you played it). A held note lasts up to the next note of the take (legato). With the
  arp on, the arp's notes are recorded, and the next round of the loop replaces the last instead of piling up.
- **Patterns:** SELECT in the sequencer or the MIXER goes to the next / previous one: stopped at once, playing on the
  next bar (the top line shows `PAT 1>2` until then); its sounds and its mix come with it on the same downbeat.
  SAVE held + a white key 1-8 goes there, + a black key copies this one there (with its sounds).
- **Saving:** SAVE in the sequencer asks `SAVE PATTERN?`, SAVE again saves (playing: at the stop). A pattern also
  saves itself 3 s after a change once the sequencer stands (the `*` after `PAT 1` goes); its sounds and mix with it.

## Build and run

OberVave is built on [Dinghy](https://github.com/zvenson/dinghy), whose platform (boot, USB, updates, audio, settings)
it keeps unchanged; the setup is Dinghy's (Linux x86-64, Python 3 with Pillow, the JieLi toolchain, three files of
the JieLi AC79 SDK: see Dinghy's README, "Set up once").

```sh
tests/run_host.sh                 # the synth on your computer: build/host/obervave.wav and screens as PNG
tests/run_host.sh my-script.txt   # your own keys / buttons / knob turns / MIDI over time (tests/host.c explains)
sh web/sim/build.sh               # the browser simulator: docs/sim (zig cc + wasm-opt), node tests/sim_test.mjs
./build.sh                        # the device package: build/obervave.fwsc
python3 tools/fm1_install.py build/obervave.fwsc   # onto the FM-1 over USB
```

The build refuses what would brick an FM-1 (a 64-bit division, a call into the chip ROM, too much RAM); the rules
are in `docs/RULES.md`, the platform calls in `docs/API.md`.

The video: `video/song.py` writes nine scripts, runs nine host instances filmed at 30 fps and mixes the stems;
`video/make_video.py` cuts the film. Both need only Python, NumPy, Pillow and ffmpeg.

## Credits and licence

GPL-3.0-only, see `LICENSE` and `LICENSING.md` for who made what. OberVave stands on
[Dinghy](https://github.com/zvenson/dinghy) (zvenson), which carries the FM-1 platform of
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments) with the changes of
[SLOOP](https://github.com/isod89/sloop-fm1) (isod89). The filter follows Andrew Simper's trapezoidal SVF, the reverb
Jon Dattorro's 1997 plate.

OberVave is inspired by the classic 8-voice poly synths of 1979-84 (Oberheim's OB-X and OB-8 era). Those names are
trademarks of their owners; OberVave is not made by or affiliated with them, and none of the marks appear in the
firmware. "M-VAVE" and "FM-1" belong to their owners too.
