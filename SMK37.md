# OberVave on the M-Vave SMK-37

OberVave also runs on the **M-Vave SMK-37** keyboards: the SMK-37 Pro, the SMK-37 Elite and the MKE-P37 (and the
Donner Starrykey 37 Play, with the Pro package). They have the same chip as the FM-1; only the panel is wired
differently. The SMK-37 build is the same synth, the same sounds and the same screens, with the SMK's extra
controls put to work: the 16 pads are the step sequencer, the faders reach the filter and the effects, the wheels
bend and add vibrato, and the 37 keys play with velocity.

## Packages and models

| Your keyboard | Package | Identity |
| --- | --- | --- |
| SMK-37 Pro, Starrykey 37 Play | `obervave-<version>-smk37.fwsc` | `SMK-37 Pro_95N` |
| SMK-37 Elite | `obervave-<version>-smk37-elite.fwsc` | `SMK-37 Elite_95N` |
| MKE-P37 | `obervave-<version>-smk37-mke.fwsc` | `MKE-P37_95N` |

N is the minor version, as on the FM-1 (`FM-1_95N`). Each package reports its own model's name, so M-Vave's own
updater still accepts that model's factory firmware afterwards. The installer refuses an FM-1 package on an SMK-37
and an SMK-37 package on an FM-1.

## Safety, before you install

The SMK-37 has data in its flash that is **unique to your unit** and cannot be downloaded again: the factory key,
pad and wheel calibration (`0xC1000`), the Bluetooth pairing, the chip's key data. OberVave never writes there: its
flash driver has an allow-list of the few sectors it may write (its settings, its 8 patterns, the update staging)
and a second, independent guard that refuses every protected range. It **reads** the calibration, to give each
key and pad its factory velocity curve.

1. **Make a full flash backup first**, as the kit describes it:
   [docs/02-update-and-recovery.md, section 3](https://github.com/genkigenki/fm1-to-smk37/blob/main/docs/02-update-and-recovery.md).
   Check that the text `trimdat` sits at offset `0xC1224` of the dump.
2. On the first start OberVave clears the stock firmware's settings area it uses (10 sectors from `0x9D000`; the
   stock settings, or another port's data there). The calibration and the way back to stock are not touched.

## Install

With the SMK-37 connected over USB (it shows up as a USB-MIDI device; under OberVave its name is "Felucca"):

```sh
python3 tools/fm1_install.py obervave-<version>-smk37.fwsc        # SMK-37 Pro (the Elite / MKE-P37: their package)
python3 tools/fm1_install.py --info                               # what is running now
```

It works from the stock firmware and from other Felucca-family firmwares. The keyboard restarts into the update
loader, writes the app and restarts into OberVave (about 3 minutes).

**USB rescue:** hold **OCTAVE-** while you switch the keyboard on: OberVave starts without sound, only the updater
(install again from there).

**The way back to stock:** install your model's factory package (`.fwsc`) with M-Vave's own updater (M-UPGRADE),
as for any firmware update. Your calibration is untouched, so the stock firmware comes back as it was.

## Controls

Everything from the FM-1 keeps its job, on the SMK button with the same meaning; OberVave's manual (README.md,
"The panel") applies. The SMK's printed names:

| SMK-37 | OberVave (FM-1 name) | |
| --- | --- | --- |
| Keys F3 .. G5 (the middle 27) | the FM-1's 27 keys | notes; in the layers steps, erase, mute / solo / tap, patterns, as on the FM-1 |
| Keys C3 .. E3, G#5 .. C6 | more keys | the notes below F3 and above G5 (OCTAVE moves them too); PARA held: erase that note |
| Velocity | | every key and pad, from your unit's factory curves (the programs' VEL>AMP and VEL>FLT set how much it matters). A key sounds when it is all the way down (its second contact): a hard strike at once, a soft one when it arrives, a very slow press after 40 ms at a soft velocity |
| **K1** | MASTER | the output level (an endless knob: 24 steps of 1024 a click) |
| **K2** | SELECT | the tempo |
| **K3** | ALGORITHM | track A / B |
| **K4** | PRESETS | the program; in the sequencer: the pattern (playing: from the loop's end) |
| **K5 · K6 · K7 · K8** | KNOB 1 · 2 · 3 · 4 | the page's four knobs, left to right as on the screen |
| **PATCH** | HOME | PERFORM; held with a layer: lock it |
| **PARA** | EDIT | OSC / FILTER pages; held: the erase layer |
| **CHORD** | ENV | the envelope pages |
| **NOTE REPEAT** | LFO | the LFO pages |
| **SCALE** | SEL | with BT held: a random sound |
| **GLOBE** | GLO | GLOBAL / VOICE / OUTPUT; held: mute, solo, tap tempo |
| **BT** | SAVE | twice: save the program; in the sequencer: save the pattern now; held: white keys go to pattern 1-8, black keys copy there |
| **ARP**, **FX**, **SEQ** | ARP, FX, SEQ | as on the FM-1 (SEQ tapped: the full-screen sequencer, K5 the step cursor, see the [README](README.md#the-sequencer); held: the keys are steps) |
| **SEQ PLAY**, **SEQ REC** | PLAY, REC | start / stop; record (held 2 s: clear the track) |
| **OCTAVE- / OCTAVE+** | OCT- / OCT+ | the octave (both: 0); SEQ held: step page; PARA held: undo / redo |
| **16 pads** | | the step sequencer, see below |
| **< / >** | | the step page of the track (1-8); the LEDs light where there is another page |
| **4 faders** | | two banks, see below |
| **FADER BANK** | | fader bank A / B (its LED lit on B) |
| **Pitch wheel** | | bend, ±2 semitones (the program's BEND on the OUTPUT page), every voice |
| **Mod wheel** | | vibrato: the program's LFO to pitch, up to ±1/2 semitone (MOD DEPTH and CC 1 stay as they are) |
| **KNOB BANK** | | held: the panel check (below) |

### The pads: the step sequencer

The 16 pads are the 16 steps of the selected track's page, in reading order: the top row steps 1-8, the bottom row
9-16. They work like the white keys with SEQ held, without holding SEQ:

- **An empty pad** sets its step at once with the last note or chord you played (notes struck together are a
  chord), at the pad's velocity. Let go, it stays.
- **A lit pad** pressed and let go clears its step.
- **Hold a pad** and the screen shows its step: **K5-K8** set NOTE, LEVEL, RATCHET and GATE. Hold a pad and tap a
  later one: one long note from the first to the second.
- **< >** turn the page (as OCTAVE- / OCTAVE+ with SEQ held, or the black keys F#3 .. A#4); **K3** picks track A
  or B. A track longer than 16 steps has a page per 16 (SEQ held + K8 sets the length).
- **While REC is on** (armed or recording) the pads play: each pad plays the last note or chord at its velocity,
  and it is recorded like the keys.

Lights: set steps in the track's colour (A light blue, B white), dimmer when a step plays by chance; the playhead
bright (red while recording); the held step red; beats 1, 5, 9, 13 glow faintly; past the track's length dark.

### The faders

| Bank | Fader 1 | Fader 2 | Fader 3 | Fader 4 |
| --- | --- | --- | --- | --- |
| **A** (PERFORM) | CUTOFF | RESO | ENV AMT | RELEASE |
| **B** (effects) | CHORUS mix | DELAY mix | REVERB mix | MASTER (the level K1 turns) |

A fader takes over only once you move it, also after a bank change, so nothing jumps. A short bar under the page
shows the value.

### The panel check

Hold **KNOB BANK**: one screen with what the keyboard reads (the flash chip, whether your calibration was found,
the ADC, faders, wheels, buttons, the last key with its velocity, and each pad's reading above its resting level).
Let go and you are back. If something does not respond, a photo of this screen helps.

## What the SMK-37 cannot do

- The SMK-37 has no key LEDs: the lit keys of the FM-1 (the notes that sound, the layers' tiles) are not there. The
  pads show the steps, the screen the rest.
- No TRS MIDI IN (OberVave has none on the FM-1 either). USB-MIDI in and out, clock, start and stop work.
- K1 is an endless knob, not a pot: the MASTER level starts at about 70 % of its travel at every power-on.

## Build

```sh
BOARD=smk37 ./build.sh                    # SMK-37 Pro:   build/obervave-smk37.fwsc        (SMK-37 Pro_950)
BOARD=smk37 MODEL=elite ./build.sh        # SMK-37 Elite: build/obervave-smk37-elite.fwsc  (SMK-37 Elite_950)
BOARD=smk37 MODEL=mke ./build.sh          # MKE-P37:      build/obervave-smk37-mke.fwsc    (MKE-P37_950)
BOARD=smk37 ./build.sh --release 1.3-beta # a release:    build/obervave-1.3-beta-smk37.fwsc (SMK-37 Pro_953)
./build.sh                                # the FM-1, unchanged (byte for byte the same as without the SMK code)
sh tests/run_tests.sh                     # the host tests (build the SMK packages first, the FM-1 last)
BOARD=smk37 tests/run_host.sh             # the SMK build on your computer (tests/host.c: pads, faders, wheels)
```

Everything SMK-only sits behind `BOARD_SMK37` (default 0): `firmware/hal/smk_input.h`, `fm1_flash_map.h` and the
SMK branches in the HAL, `firmware/src/smk_store.h` (the flash layout), `firmware/src/obv_smk.h` (the controls).
`smk37-port.yaml` lists every mapping decision.

## Credits

The SMK-37 board layer (input, display, pad LEDs, flash map, update path, package layout, installer, the wheel and
the tests it brought) is the reference port's, by M. Geisler (genkigenki), from
[genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37`, with the hardware facts of the
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit; Felucca by Leo Kuroshita (Hügelton Instruments) and
SLOOP by isod89 underneath, as on the FM-1. "M-Vave", "SMK-37", "MKE-P37" and "Starrykey" belong to their owners;
OberVave is not made or endorsed by them.
