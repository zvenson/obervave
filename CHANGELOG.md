# Changelog

OberVave for the M-VAVE FM-1 (and, as a beta, the M-Vave SMK-37). Newest first. Each release is on
[GitHub](https://github.com/zvenson/obervave/releases) and on [the Firmware Hub](https://thefirmwarehub.com/firmware/obervave/).

## 3.5.2 – 2026-11-13

- **Locked to an external MIDI clock:** with a master that sends START and clock (SYNC AUTO), the sequencer, the
  tracks and the arp now stay on the master's beat for good. Before, the clock only set the tempo and the beat slowly
  slid away (up to half a second in five minutes). Each quarter is now gently nudged back onto the master's (at most
  ~3 %, inaudible); a tempo change takes effect on the next beat, and the tempo is there after one quarter instead of
  four, so a master that starts its clock with PLAY leads from the second beat. Without a START (the clock running,
  OberVave started with its own PLAY) it follows the tempo as before. Written and tested on an FM-1 by adub671
  ([#25](https://github.com/zvenson/obervave/pull/25)), reported by dmochen and others
  ([#10](https://github.com/zvenson/obervave/issues/10), [#17](https://github.com/zvenson/obervave/issues/17)) –
  thank you!
- Identity `FM-1_9535` (as 3.5).

## 3.5.1 – 2026-11-13

- **MASTER works again:** VOLUME, DJ FILTER, DRIVE and COMP on the mixer's MASTER row now change the sound as you turn
  them – before, they only took effect with the next pattern change. Reported by dmochen
  ([#19](https://github.com/zvenson/obervave/issues/19)) – thank you!
- The browser simulator has the same fix.
- Identity `FM-1_9535` (as 3.5).

## 3.5 – 2026-11-12

- **Saving made simple: MEMORY.** SAVE opens one page with the 8 memory slots. A slot holds everything: the steps of
  all four tracks, their four sounds, the mixer, the effects and MASTER. KNOB 1 (or PRESETS) picks the slot, KNOB 2
  (or SELECT) the action, SAVE does it: **SAVE**, **LOAD**, **COPY**, **PASTE**, **INIT** and **SOUND** (the chosen
  track's sound into one of the 99 programs). SAVE, SAVE saves what you hear into its slot.
- **No auto-save any more.** What you play is a working copy of the slot loaded last; a `*` shows changes not saved
  yet. LOAD the same slot again and you are back to how it was saved. Anything that would throw changes away, or
  overwrite another slot, asks once more. Changing patterns with unsaved changes warns first. (Global settings such
  as tempo and MIDI channel still keep themselves.) Thanks to the player on the hub who asked for exactly this.
- Gone: SAVE held + keys 1-8 (go / copy), SAVE PATTERN?, the PATTERNS view, the program "save as". SAVE held + SELECT
  still dices a new sound.
- **Backup:** programs, patterns and settings to a file on your computer and back, from the browser:
  [thefirmwarehub.com/tools/obervave-backup](https://thefirmwarehub.com/tools/obervave-backup/). Asked for by
  dmochen ([#15](https://github.com/zvenson/obervave/issues/15)).
- **VIBE LINK:** the FM-1 as a controller for the VIBE FM from inside OberVave (OUTPUT page, SELECT pressed: OFF / ON /
  CONTROL). ON: OberVave plays on and its knobs, buttons and keys also go to the VIBE; CONTROL: only the controller
  (OCT- and OCT+ held: back).
- Identity `FM-1_9535`.

## 3.3.2 – 2026-11-02

- OberVave's screen, live and as big as you like, on a computer or phone with
  [FM-1 Mirror](https://github.com/pfkellogg/fm1-mirror) by Patrick F. Kellogg. Sent only while FM-1 Mirror asks for it.

## 3.3.1 – 2026-11-02

- Newer FM-1s carry another flash chip (UC25HQ80): OberVave saved to RAM only on them and refused updates. Fixed
  (thanks to Paul Nederhoed).

## 3.3 – 2026-10-31

- Fatter: **LP 24 is a real four-pole ladder** with the resonance around all four stages and a soft stage in the loop,
  so the resonance compresses instead of thinning and the bass stays.
- No hard clipping in a voice any more: the oscillator mix and the filter output go through soft stages; DRIVE goes from
  warm to fuzz. VINTAGE moves the cutoff too, each voice on its own.
- VOICE (mode, unison, detune, glide) is under EDIT after OSC 2; GLO is GLOBAL / OUTPUT.

## 3.0.1 – 2026-10-23

- Its own identity `FM-1_9530` (3.0 shared one with other firmwares, so the hub could not tell them apart).

## 3.0 – 2026-10-23

- **Four tracks** A-D on the eight voices, each its own sound, arpeggiator and sequence.
- A **mixer** (level, pan, sends to chorus, delay, reverb and drive, mute, solo, meters), **DRIVE** and a **MASTER**
  (DJ filter, drive, compressor, limiter).
- Tracks start and stop on the bar, like clips; every pattern keeps its own sounds and mix.
- Recording with a quantise per track, nudge, legato, arp takes and recorded knob moves; SYNC AUTO / INTERNAL.
- 1.5's programs, settings and patterns come over on the first start.

## 1.5.3 – 2026-10-19

- The update loader shows what it does, in the Firmware Hub's look (the idea from Skiff).

## 1.5.2 – 2026-10-19

- The FM-1's **TRS MIDI IN** jack works (through a TRS-to-DIN adapter, type A or B); the keys send on the set MIDI channel.

## 1.5.1 – 2026-10-18

- 1.5, built and signed by GitHub Actions.

## 1.5 – 2026-10-16

- 99 programs: 64 factory sounds (33-64 new), save a sound into any slot.

## 1.4.3 – 2026-10-16

- HOLD per sound: track A's and B's arpeggiators latch on their own.

## 1.4.2 – 2026-10-16

- Notes stay in time while the screen redraws (chords and CC streams no longer stumble).

## 1.4.1 – 2026-10-16

- Each sound its own arpeggiator.

## 1.4 – 2026-10-16

- Two sounds: track B can play its own program; the TRACKS view.

## 1.3.1 – 2026-10-15

- The sequencer coloured by track (A blue, B orange).

## 1.3 – 2026-10-15

- The sequencer on the full screen; patterns switched on the loop's end.

## 1.2.1 – 2026-10-13

- Held function buttons take the knobs on the GLOBAL page too.

## 1.2 – 2026-10-13

- The step sequencer (two tracks, 256 steps, chords, ratchets, chance), the click, USB-MIDI out fixed.

## 1.1 – 2026-10-13

- The first release: 8 voices, two oscillators with sync and cross-mod, 12 / 24 dB filter with the SEM-style morph,
  two envelopes, LFO, arpeggiator, chorus, delay, plate reverb, 32 programs. Do not install it: its USB-MIDI out is
  broken (1.2 fixed it).
