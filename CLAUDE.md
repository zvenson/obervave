# OberVave

A firmware for the M-VAVE FM-1: ONE polyphonic analog-style synth in the spirit of the classic 8-voice American
poly synths of 1979-84 (OB-X / OB-8 era). Built on Dinghy (`../dinghy`, zvenson/dinghy, started from d44df7d).
GPL-3.0. Identity `FM-1_95N`. Name by the user ("OberVave": Ober... + M-VAVE).

## Hard rules

- Nothing from SLOOP / sloopDX / zp12 apps (no grooves, no other engines; no code CTL from their sequencers). Only this
  one synth, as deep as possible. Exception asked for by the user on 2026-10-13: an own step sequencer in the style
  of Baud Girl's FM-1+VA (chords, ratchets, chance; Baud Girl is closed source: the idea only), two tracks sharing
  256 steps (`obv_seq.h`). Dinghy's platform (`platform.c`, `usb.c`, `ota.c`, hal/) stays as it is.
- No trademarks in the product: never "Oberheim", "OB-X", "OB-8" on screen, in patch names or in the release name.
  In docs only as "inspired by"; the marks belong to their owners. No artist or song names in patch names. The same
  for "Stranger Things" (Netflix): the user wants that series' dark-arpeggio sound (its theme was played on an
  Oberheim Two Voice, so this synth's family), the name stays OberVave, and the show is never named in the product.
- Keep the Felucca / SLOOP / Dinghy credits (LICENSING.md, README).
- Target: JieLi AC791N, no FPU, no libgcc: integers only, **no 64-bit division** (int64 multiply is fine),
  no constant in 0xFFC00000-0xFFD00000 (`tools/build.py` checks). RAM <= 96 KB, big buffers in `.pool`.
- Audio ISR: 256 frames per call at 44.1 kHz (5.8 ms). Stay under half of it. Measure on the device
  (`fm1_ticks()` around the render, show it on the GLOBAL page).

## Where things are

- `firmware/src/obervave.c`: the app (panel, MIDI, storage, what to draw when). Includes `platform.c` first, then
  `obv_dsp.h` (oscillators, SVF, tanh), `obv_params.h` (the parameter table, pages), `obv_fx.h` (chorus, ping-pong
  delay, plate reverb: after the voices in the ISR), `obv_engine.h` (voices, envelopes, LFO, arp: the ISR),
  `obv_patches.h` (32 factory programs), `obv_seq.h` (the sequencer: pattern, factory patterns, the ISR player),
  `obv_ui.h` (the drawing).
- Sequencer (`obv_seq.h`): `pattern_t` = magic, 2 tracks {len, div, swing 50-75, transp, mute}, 256 steps {4 notes,
  vel, gate (127 = TIE), ratch 0-3, chance 0-100}. Track A's step i is `st[i]`, B's is `st[255 - i]` (a length change
  never moves the other's steps), lenA + lenB <= 256, A >= 1, B 0 = off. `seq_tick` per CTL block after `arp_tick`,
  straight into `voice_trigger` / `voice_release(n, 1)` (not through the arp). 8 patterns, one flash sector each
  at 0xF4000.. (`blob_load/save` in platform.c, `FL_RANGE_OK` widened; the host keeps them in RAM); `set.pat` is
  the current one. Saves wait for the sequencer to stop and the voices to be quiet (the erase stalls the audio).
  Panel = sloopDX's (the user: "die Tastaturbefehle wie bei sloopDX, sonst nützt das keiner"; repair, not a port):
  a function button tapped (let go untouched, < 600 ms) opens its pages, held it is a layer (`layer`: L_SEQ steps,
  L_EDIT erase + SHIFT / LENGTH x2,1/2 / TRANSPOSE knobs + OCT undo/redo, L_GLO keys 1/2 mute, 5/6 solo, G5 tap
  tempo, L_SAVE keys 1-8 pattern (+ SEL pressed: dice), L_FX the effects' mixes); HOME while held locks it, its
  button (or ENV/LFO/ARP, a page change) lets it go. SEQ layer: empty step pressed = set at once with the pen chord
  (the last notes struck together), a set one pressed and let go = cleared, hold + knob NOTE / LEVEL / RATCHET /
  GATE, hold + a later step = a long note, black keys F#3..A#4 = page 1-8, OCT page; no step held NOTE (the pen) /
  DIV / SWING / LENGTH; set steps lit, playhead inverts, keys 1 5 9 13 glow. ALGORITHM = track A/B, SELECT =
  tempo, OCT both = octave 0, PERFORM = CUTOFF RESO ENV AMT RELEASE. REC / PLAY / REC held 2 s as before; patterns
  start empty; REC with the arp records the arp's notes. HOLD stays on the ARP button on its page.
  **SEQ tapped (2026-10-15, the user: "wie Sloop, Steps mit dem Rädchen anfahren, das ganze Display")**: the
  full-screen sequencer (`seq_view`: STEPS / STEP+ / TRACK, SEQ tapped again cycles, HOME leaves; drawn by
  `ui_seq_*` in obv_ui.h in four strips: top line, piano roll of the cursor's 16, info line + 64-step overview,
  knob strip). KNOB 1 = step cursor, keys write note/chord into it and the cursor goes on when let go (key held +
  KNOB 1 = tie on), EDIT tapped clears the step; STEPS: NOTE GATE VELO, STEP+: RATCHET CHANCE ON/OFF, TRACK: LENGTH
  DIV SWING TRANSP, PATTERNS (8 slots: K1 slot, K3 GO, K4 COPY HERE). Patterns as zp12's loops (the user: "so wie im
  zp12, da ists super gelöst"): PRESETS on the sequencer = pattern 1-8, stopped at once, playing from the end of
  track A's loop (`pat_q`, `ctl_pat_swap`: the ISR swaps it in, the one left goes to `pat_old` and is saved at the
  stop); SAVE tapped = save now (playing: at the stop); SAVE held + white 1-8 go, + black 1-8 copy (a used slot:
  press again). Autosave 3 s after an edit once stopped. KNOB 1 on an off track gives it 16 steps; a fast LENGTH
  turn jumps by 16. Host scripts: tests/seq_view.txt, seq_trackb.txt, seq_patterns.txt. Released in 1.3 for both boards.
  **Two sounds (2026-10-16, a user of the hub: "if the sequencer could play 2 sounds in 2 tracks")**: track B plays
  sound A, or its own program: `trk[1].pad[0]` = B_PROG (program + 1, 0 = A), `pad[1]` = B_DRY (0 = all of B into
  the effects, 127 = all dry), so old patterns stay as they were. The engine has two parts (`part_t pt[2]`: patch,
  LFO, the per-block numbers; `voice_t.part`; `live`, `live_b`, `live_bq` = the queued pattern's B, swapped in by
  the ISR with the pattern); one sound's VOLUME is now in its voices, the output stage has only CC 7. MONO / UNISON
  take cards 0-3 (A) / 4-7 (B) when B has its own sound, POLY shares the 8 unless the other sound is a group.
  B's voices render into `bus_b`: its send joins A before the output stage and the effects, the rest gets its own
  output stage after them. The effects stay A's (`is_a_only`); **each sound has its own arp** (the user 2026-10-16: "der Arpeggio geht nur auf
  einer Spur"): `held[2][]`, `nheld[2]`, `arp[2]`, `arp_tick(p)`, arp_q carries the part; the pedal holds both, HOLD is per sound (`hold` / `ctl_hold` bit p, ARP on its page toggles the panel's sound's; tests/seq_two_holds.txt). The panel is on B's sound when track B is chosen
  and has one (`ed_b()`, `ed_patch(p)`): keys (`play_note`, a note's part kept for its release), pages, PRESETS,
  SAVE x2, randomize; the header in B's orange. **TRACKS view** (SEQ view 0, the user 2026-10-16: "wie bei sloop,
  die Spuren untereinander"): A and B rows stacked (`ui_seq_track_row`: tile, sound, length, 16 steps, playhead),
  ALGORITHM picks the row, PRESETS = the row's sound (B: 0 `= A`, up = A's program as B's own first), knobs LENGTH
  LEVEL CUTOFF FX SEND; PRESETS stays the pattern on the other views. Then STEPS, STEP+, TRACK, PATTERNS.
  MIDI: with a channel set, channel + 1 = B's sound (notes, CC, program change). Cost on the host: one sound +4 %
  (~1010 instr/sample on seq_view.txt, was 970). Host scripts: tests/seq_two_sounds.txt, seq_two_patterns.txt.
  The click (set.click, the old pad byte: OFF / REC / ON / COUNT-IN, GLOBAL page KNOB 2; the CPU time moved to that
  page's title): a 6 ms square after the effects, 2.4 kHz on beat 1, 1.6 kHz else (obv_engine.h); COUNT-IN: REC
  armed + PLAY counts a bar (ctl_count, the tracks start after it, notes in it only sound).
- Effects (`obv_fx.h`): all lines int16 in `.pool` (274 KB used of 336). Chorus: one line, two taps a quarter turn
  apart, 4-point Hermite reads. Delay: two 743 ms lines, ping-pong, time in note values of the tempo (1/16 .. 1/2,
  dotted, triplet), the read position slews like a tape when the time changes, a one-pole in the feedback (TONE).
  Reverb: Dattorro's plate (1997) at the full rate, two swaying allpasses in the tank, SIZE = decay 0.37 .. 0.96,
  DAMP, PRE-DLY up to 120 ms; `REV_DIV 2` compiles it at half the rate if the chip needs it (untested on the
  device). Host cost (instr/sample): chorus ~160, delay ~110, reverb ~550; worst case with all three ~2350.
  **The user's priority is maximum sound quality over CPU**: measure on the device before trading any of it.
- Filter type SEM (`F_SEM`): the 12 dB SVF's LP .. notch .. HP by MORPH (FILTER + page, knob 4), as the SEM's pot.
- Input during drawing (2026-10-16, a video "THIS FIRMWARE NEEDS A FIX": notes stumbled under CC streams, chords on
  the SMK-37 came in staggered): the keys and MIDI are `keys_in` / `midi_in` (obervave.c); the main loop calls them
  once a pass, and `app_idle` (notes, CC, clock only; a program change waits in `midi_back`) runs them while lcd.c
  waits for a strip's DMA (`LCD_IDLE`, `fm1_lcd_done` in hal) and between ui_page's knobs, only while `idle_ok`
  (the screen section). A note redraws the page only where it shows (`notes_shown`: SEQ, REC). The host can't show
  it (its screen takes no time): sound and screens there are bit-identical to before.
- `tools/gen_tables.py` -> `firmware/gen/obv_tables.h` (committed): pitch, SVF g, tanh, envelope / LFO / glide
  coefficients, pan, cos.
- The logo: `tools/make_logo.py` -> `assets/logo*.svg|png` (red glowing serif outlines, three blue bands through the
  letters; `logo_svg()` draws it with parameters). `--splash` -> `firmware/gen/obv_splash.h` (committed): the boot
  screen, 240 x 49, two layers (letters; letters + bands) RLE over one RGB565 palette, ~31 KB; `ui_splash` strikes
  it like a neon tube (flicker), sweeps the bands in, fades the line under it in, 2.4 s. Pillow's quantizer + the
  panel colour snapped to C_PANEL so the background is seamless.
- Installing on the FM-1: `python3 tools/fm1_install.py build/obervave.fwsc --yes` (USB-MIDI, ~3 min, `--info`
  shows the identity). Works from SLOOP (FM-1_971) and from OberVave itself.
- `tests/host.c` + `tests/run_host.sh [script]`: the same app on the PC (WAV + screen PNGs, scripted keys /
  buttons / MIDI / `enc ID N` encoder turns). Film mode (`build/host/obervave OUT script FPS`) writes a frame every
  1/FPS s and `OUT-log.txt` (keys, key LEDs, button LEDs, encoder positions per frame). `OBV_PERF=1` prints the
  instructions per sample (Linux perf counter): the yardstick is sloopDX's engines at ~1000-1500; OberVave's worst
  case (8 voices, LP24, sync + x-mod, saw+pulse, drive) is ~1900, BRASS STAB ~1600. Not yet measured on the device:
  the GLOBAL page shows the render time in us (budget 5800, aim < 2900).
- MIDI in: USB and (FM-1) the TRS jack share one ring (`midi_in_q`). MIDI CC: 74 cutoff, 71 reso, 73 amp attack, 72 amp release, 5 glide, 1 mod depth (the wheel and the knob are one
  value), 7 volume, 64 sustain, 120/123 all off; every other parameter from CC 20 up in table order, skipping 64
  and the fixed ones (`cc_map_init`): the first 50 parameters take 20..63 and 65 (ARP GATE), the appended ones
  66 MORPH, 67-69 chorus, 70 delay time, 75-77 delay, 78-81 reverb. Program change picks the program. New
  parameters go at the end of the table so these numbers stay.
- The video: `video/song.py` (nine OberVaves from the host build, D Phrygian 94 BPM, 40 bars: slow dark disco,
  the four-note arpeggio all the way through, four on the floor, a woozy unison lead with octave dives; stems/,
  f/<part>/ frames + logs for all nine, track.wav; `--mix-only` re-sums the stems with new gains) and
  `video/make_video.py` (a shot list: focus / duo / trio / wall of nine, the kick pulses, the clap flashes, a cut
  tears the colour planes for three frames; the video starts on the music's first beat with sound, and for the first
  4 s the logo strikes over it with lightning flashes, `INTRO`; the end card says thefirmwarehub.com) ->
  `video/obervave-1.2.mp4` (copied with thumb + YOUTUBE.md to ~/youtube/fm1-hub/). The renders are git-ignored.
  The kick and the hats are programmed on OberVave's own step sequencer by key presses (`Part.program_steps`),
  so the drum screens show the SEQ page; the clap stays on MIDI.
  **The arp and the delay run on the device tempo (settings, 120 BPM at first boot), not on the script's grid**:
  every part dials the song's tempo in on the GLO page first (`Part.setup`), or the arpeggio restarts off the beat
  at every chord change (that was the "springt am Anfang" of 1.1's first cut). The host's splash takes 2.4 s: a
  script's first key must come after it (T0 = 9 s here: the tempo turn runs 2.5 .. 3.2 s, the drum programming
  ~3.6 .. 8 s; the films run 15 s past bar 40 for the end card).
- `tools/build.py`: `./build.sh [--release X.Y]` -> `build/obervave[-X.Y].fwsc`, identity FM-1_95Y.
- Docs from Dinghy: docs/API.md (platform calls), docs/RULES.md (the chip), docs/MAKE-IT-YOURS.md.
- Workspace file: `../obervave.code-workspace` (opened in VSCodium Insiders, flatpak).

## Status (2026-10-13, third session)

**3.3.1 (2026-11-02, tag v3.3.1, FM-1_9533):** only the UC25HQ80 flash (JEDEC 0xB36014, newer FM-1s from AliExpress) in
`FL_JEDEC_OK` (hal/fm1_flash_map.h) beside the Puya 0x856014 – app, recovery, update loader; on it 3.3 saved to RAM only and
refused updates (Paul Nederhoed). The user: fix OberVave, not sloopDX. Not tested on that chip before the release (Paul cannot
flash a file himself: the hub's installer is his way). The SMK-37 packages stay 3.3-beta.

**3.3 (2026-10-31, tag v3.3, FM-1_9533, "Oberheim wäre massiver"; heard by the user on the FM-1 and released the same day, the CI's attested packages in docs/firmware (FM-1) and docs/firmware/smk37 (3.3-beta, tag smk37-v3.3-beta); the native VIBE 3.3 carries the same engine, vibe-sdk 03010dc):** LP24 is a
real four-pole ladder (`lad_coef` / `lad_step` in obv_dsp.h: four TPT one-poles, the resonance fed back around all four,
zero-delay linear solve, a tanh on the loop's input, input gain 1 + k/2 so the bass stays; was two cascaded SVFs with the
resonance on the first). The mixer's hard clamp and the post-filter clamp are gone: `sat_in` (tanh, unity below ~30000,
DRIVE's gain 1..16 in front of it, always on) and `sat_knee` (linear to 32768, tanh to 65535). The vintage walk moves the
cutoff too (~1/4 semitone at the default). Levels within ~1 dB of before (tests/fat.txt + the ana script in the
session: brass -6 dBFS peak), a full-resonance cutoff sweep stays bounded (tests/fat_sweep.txt). Host cost +7 %
(1653 instr/sample on fat.txt, was 1544). `TANH_Q15` is tanh(x/32768), unity slope (its old comment said x2).
Not done yet: oversampling, the SVF's integrator saturation, retuning the brass patches.

**3.0.1** (2026-10-23, tag v3.0.1: identity FM-1_95XY from now on, FM-1_9530, its own; 3.0 (tag v3.0) reported FM-1_950, shared with CS-1, OPN-1, FMEL-1, so the hub could not tell them apart; the CI's attested package in docs/firmware; the SMK-37's 3.0.1 packages (SMK-37 Pro / Elite / MKE-P37_9530, the CI's) in docs/firmware/smk37 too, tag smk37-v3.0.1-beta, the user's go 2026-10-23): the VIBE edition
(~/VibeFMmk1/vibe-sdk/firmware/obervave, OberVave 3.0 native; the FM-1-code 2.0 edition is in that repo's tag
vor-aufraeumen-2026-10-22) back on the FM-1 and the SMK-37: 4 tracks A-D on the 8 voices (MONO/UNISON groups pick their
cards: group_pick), mixer (level, pan, sends chorus/delay/reverb/drive, mute/solo, meters), DRIVE and MASTER (DJ
filter, drive, compressor, limiter) in fixed point, tracks start/stop on the bar, every pattern keeps its sounds and
mix (psnd_t behind the pattern in a 4-sector slot: FM-1 0x99000.., SMK 0xC2000.. in FL_SMP; 1.5's patterns and
settings convert at boot, the old ones stay), 3.0's recording (quantise per track, nudge, legato, arp takes, knob
lanes), SYNC AUTO/INTERNAL. Panel: ALGORITHM track, PRESETS its sound, SELECT bar (STEPS) / next pattern (sequencer,
MIXER) / tempo, SELECT held = STEP+ (GLOBAL: SYNC), HOME again = MIXER and its rows, GLO held = keys 1-4 start/stop
(REC: record), 5-8 mute, 9-12 solo, G5 tap; SAVE in the sequencer asks. Host worst case ~2460 instr/sample. Tests:
tests/v3_*.txt, OBV_V15=1 simulates a 1.5 store. Shine On got the same (its own CLAUDE.md).


Released: **1.5.3** (2026-10-19, FM-1_955, tag v1.5.3: the update loader's screen, `firmware/loader/ldr_screen.c` (the hub's look; Skiff's idea, illuminus5): name, status, 20 segments with a running light while it waits, the percent while it writes, thefirmwarehub.com; FM-1 only, the SMK-37's loader stays dark; tested on the user's FM-1, two installs) after **1.5.2** (2026-10-19, FM-1_955, tag v1.5.2 (v1.5.1 = 1.5 rebuilt by the CI): the TRS MIDI IN on (FELUCCA_UART on the FM-1 board, sloopDX's
midi_uart.c polled at 2 kHz in TIMER5; users said MIDI from external gear did not work), the keys send on the set
channel (`MIDI_OUT_CH`); installed on the user's FM-1, USB-MIDI and a second update fine, the jack itself not tried here;
SMK-37 1.5.2-beta) after **1.5** (2026-10-16, FM-1_955, tag v1.5: 99 programs, 64 factory (33-64 new), 65-99 INIT; save as: SAVE, PRESETS = the slot, SAVE; the programs in 2 own flash sectors `psect` (FM-1 0x97000, SMK 0xA6000, 50 a sector), the settings sector only globals now (SET_VERSION 3, a v2 store's 32 programs move over at boot); SMK-37 1.5-beta; installed on the user's FM-1 and boots) after **1.4.3** (tag v1.4.3: HOLD per sound, `ctl_hold` bit p) after **1.4.2** (tag v1.4.2: keys and MIDI read while the screen draws; released without a device test) after **1.4.1** (tag v1.4.1: each sound its own arp) after **1.4** (tag v1.4: two sounds, track B's own program; the TRACKS view; tried on the
user's FM-1 first) and **SMK-37 1.4-beta** (docs/firmware/smk37, tag smk37-v1.4-beta, not yet on a real SMK-37).
Before: **1.3.1** (FM-1_953, tag v1.3.1: the tracks coloured, A blue / B orange) after **1.3** (tag v1.3: the full-screen sequencer as SLOOP's step page, patterns as
zp12's loops); SMK-37 1.3.1-beta was tested on a real SMK-37 on 16.10.2026: works.
Before: 1.2.1 (FM-1_952; 1.2 replaced: a held function button's knobs
went to the GLOBAL page's settings). The hub (thefirmwarehub.com, hosted at Hetzner, the
user 2026-10-15; its catalogue: fm1.designburgapps.com/catalog.json) reads the newest package in docs/firmware via
the GitHub API when it syncs: cron every 3 h (1, 4, ... 22 h) `/srv/fm1/sync.sh` on `ssh hetzner-fm1-user`, log
/srv/fm1/fm1live/sync.log; run it by hand after a release. 60 calls an hour without a token, so a sync can hit the limit.
The card's text is in zp12repo's tools/fm1_sync.py.

Later on 2026-10-13: **the step sequencer** (two tracks, 256 steps, chords, ratchets, chance, step + live record,
8 patterns), host-tested (timing 1/16 at 120 BPM checked in the WAV, screens filmed), device build passes. **Not
on the device**: updates from the running OberVave fail (the FM-1 stops answering after the first read request,
"update failed" on the screen; the identity handshake answers only sometimes, later not at all; sound and screen
run normally). Not the effects' load (all off: no change). The first install from SLOOP worked; the rescue mode
(OCT- held at power-on) did not come up when the user tried. Diagnosis (18:40-19:00, host kernel log via
`flatpak-spawn --host journalctl -k`): **every packet the FM-1 sends on the MIDI IN endpoint (0x81) babbles**
(`usb 3-9: urb status -75`, -EOVERFLOW, dozens a second), the hub disables the port ("disabled by hub (EMI?)") and
the FM-1 comes back as a new device with a cold boot (a held note is gone; five quick ones did not trip the boot
guard, so not warm resets). Host -> FM-1 MIDI works (notes play, USB audio records fine with `parecord`). Since
17:42, the first minute of OberVave on the device (keys send MIDI out too). Not the CPU load (idle, effects off:
the same), not the LCD, not the descriptors (EP 0x81 bulk 64, EP 0x84 iso 196, total length right). The USB code
and HAL are byte for byte sloopDX 3.4+'s; zp12 (FM-1_971, worked here) has FELUCCA_UAC 0 and no 48 kHz. Not the USB audio either: a build with FELUCCA_UAC 0 (bcdDevice 3.00, MIDI
only) babbles the same. The Felucca update loader (same usb.c, polled, interrupts off) and zp12 (usb_poll every
0.5 ms from TIMER5, also nested in the audio ISR) send fine; OberVave/Dinghy's platform defers usb_poll while
in_audio ("owed"). Not that, not the alignment of ep1tx either (32 now, no change). Bisected: Dinghy 0.3 sends fine; Dinghy's app
on OberVave's platform babbled; **the difference was -O2** (OberVave's APP_CFLAGS, the unity build puts usb.c in
it). Built with -Os the FM-1 answers every time and installs work again (2026-10-13 21:10). Which line -O2 breaks
is still unknown; the engine's render time at -Os is not measured on the device yet (the arp ran clean).
**The way back on (worked 2026-10-13 20:20):** send the UBOOT key (F0 22 24 35 7D F7, host -> FM-1 works) -> the
mask ROM's "WL80UBOOT1.00" (USB mass storage, /dev/sg0, root) -> jl-uboot-tool's wl82loader.bin (MIT tool,
JieLi blob, at 0x01C02000, arg 0x0001: SPI NOR) -> its LoaderV2 reads / erases / writes flash (read_id answers one
byte late: 0x008560) -> compare the app area 0x4000..0x93000 with the old package's (tools/fm1pkg_make.py
flash_image, key 0x980F), write only the sectors that differ, verify each, run_app. Our Felucca loader started
straight from the ROM cannot read the flash (SPI0 not set up: it stops after 2 requests); started from the helper
loader the chip resets. The rescue key (OCT-) read nothing: FM1_SETTLE_US raised to 100 us (untested).

Third session added the effects (chorus / delay / plate reverb, three FX pages), the SEM filter morph, the dark
arpeggio programs (01 DARK ARP, 26 UPSIDE ARP, 29 NEON PAD; BRASS STAB moved to 13) with effects on most of the
others, the blue look, and the second video. Patches are 80 bytes (`SET_VERSION` 2). Later that day the package
(112 KB image, 21 KB RAM, 274 KB pool) was **installed on the user's FM-1** (it came up as FM-1_950, the install
and the restart worked) with the logo splash; the user still has to report the CPU meter (GLO page) with DARK ARP
and with all three effects on a big patch, the knobs' feel, the splash. If the render time is over ~4000 us,
`REV_DIV 2` (half-rate reverb) is the first lever; ask the user before taking any quality away. CUTOFF and RESO slew ~23 ms per block (CC sweeps glide). Open: the
look's polish, MIDI clock is tempo-following only, the arp steps on 16-frame blocks, the delay time does not follow
an external clock's tempo changes smoothly, a tempo CC (the video sets it with the knob).

## Plan (agreed in the first session, 2026-10-12)

Priorities from the user: **the audio engine first, the look second**. Max depth of the one synth.

### Engine (all in the ISR, control rate CTL = 16 frames)

- 8 voices. Modes: POLY (round-robin allocation over the 8 "voice cards", steal oldest released, then oldest),
  DUAL (2 detuned voices per note, 4 notes), UNISON (2-8 voices on one note, detune spread, pan spread), MONO
  (last-note priority, legato without retrigger). Glide in every mode (also poly).
- Per voice: OSC1 + OSC2, each SAW / PULSE / SAW+PULSE / OFF, PW + PWM from the LFO. OSC2: semis -24..+24,
  fine detune, HARD SYNC to OSC1, FILTER ENV -> OSC2 pitch (sync sweeps), X-MOD (OSC2 -> OSC1 linear FM, clamp
  the step >= inc/16). Noise (per-voice xorshift). Free-running phases, random start.
- Anti-aliasing: polyBLEP as discontinuity events with a one-sample delay, so natural wraps AND sync resets use
  the same code: a jump of height H at fraction t (time from the event to the current sample, 0..1) adds
  `prev += H/2 * t^2`, `cur -= H/2 * (1-t)^2`. Pulse = saw(ph) - saw(ph + pw) (two event streams, mean 0).
  One 32-bit division per wrap (t = ph / (inc >> 15)), as sloopdx dsp.c `blep`.
- Mixer -> DRIVE (tanh table) -> filter. Filter types: 12 dB state-variable (Simper trapezoidal, Q13 coefficients
  per block, as sloopdx dsp.c `tsvf_*`) LP / BP / HP / NOTCH with a soft saturation on the band-pass state (warm,
  bounded self-oscillation); 24 dB LP = two cascaded SVFs (resonance on one). Cutoff in Q8 semitones:
  base + filter env * amount (bipolar) + key track 0..100 % + LFO + velocity + per-voice drift; a generated
  table tan(pi f / fs) (tools/gen_tables.py -> committed header).
- Envelopes: filter ADSR + amp ADSR, analog RC curves (attack aims at ~1.3 and stops at 1.0), per-block
  coefficients from a time table 0.5 ms .. 10 s, amp interpolated per sample (no zipper).
- LFO (global like the originals, phase mode SYNC / FREE / SPREAD = per-voice phase offsets): TRI, SAW, RAMP,
  SQUARE, S&H, smooth random; rate 0.05-30 Hz, delay / fade-in per voice; amounts to pitch (OSC1 / OSC2 / both),
  PWM, filter, amp. Mod wheel (CC1) adds to pitch and filter depth.
- VINTAGE: per voice card fixed offsets (osc tune +-8 cents, cutoff +-1.5 semis, env times +-10 %) plus a slow
  random walk on pitch, all scaled by the knob. SPREAD: per-voice pan (alternating L/R, the originals' pan pots),
  equal-power gains. Output: sum -> gentle tanh "output stage". Nominal chord peak ~ -6 dBFS before MASTER.
- Pitch: Q8 semitones -> inc via a 12-entry octave table (Dinghy's INC_C9) x a 256-entry fine table, int64
  multiply, shift by octave. Pitch bend (range param), MIDI CC for every parameter (CC 74 cutoff, 71 reso,
  1 mod, 64 sustain, 5 glide, 73 attack, 72 release, 7 volume, the rest from CC 20 up), sustain.
- Arpeggiator in the ISR (sample-exact): UP, DOWN, UP/DN, RANDOM, AS PLAYED; 1-4 octaves; 1/4 .. 1/32 + triplets;
  gate %; tempo 40-240; latch. MIDI clock if 0xF8 reaches midi_get (check usb.c).
- Main loop -> ISR: a ring of note events {on/off, note, vel} (one writer each side); the ISR owns voices,
  held-note list, arp, sustain. ISR -> UI: per-voice level + note (volatile), and a scope capture (armed by the
  main loop, triggered on a rising zero crossing, stride from the last note's period).

### Patches

- A patch = a byte array indexed by a parameter table {label, min, max, display kind} + a 10-char name. The UI,
  MIDI CC, randomizer and storage all walk that table.
- 99 programs (1.5; was 32): 64 factory in const flash (`FACTORY[NFACT]`, `FACT_INIT` for 65-99), the user's in
  `psect[2]` (own sectors, `prog_put`, saved with the settings when silent: the erase stalls the audio).
- Factory sounds to make it shine: big brass stab, poly brass swell, sync-sweep lead, unison lead, string
  machine-ish PWM pad, bell-ish x-mod, fat bass (mono), arp sequence, sweep pad, 12 dB band-pass "quack" etc.

### Controls

- PRESETS = program, ALGORITHM = CUTOFF (always), SELECT = page scroll, KNOB 1-4 = the page's four parameters.
- HOME = PERFORM (RESO, F.ENV AMT, A.RELEASE, MOD DEPTH). EDIT cycles OSC1 / OSC2 / VOICE (since 3.3, as native 3.2) / OSC MOD / FILTER / FILTER +,
  ENV cycles FILTER ENV / AMP ENV, LFO cycles LFO / LFO DEST / LFO +, FX cycles CHORUS / DELAY / REVERB,
  ARP = arp on/off + its page, GLO cycles GLOBAL (TEMPO, MIDI CH, TUNE, CPU meter) / VOICE (mode, unison, glide) /
  OUTPUT (vintage, spread, bend, volume).
- (Superseded 2026-10-13 by the sequencer, see above.) PLAY = HOLD, SEQ = UNISON quick toggle, REC = randomize, SAVE
  = save (twice to confirm, PRESETS picks the slot), OCT- / OCT+ = octave. Key LEDs show sounding notes.

### Look (240 x 240)

- Own design, no copied trade dress, in the colours of the originals (the user: "the Oberheim is blue"): blue-black
  panel, near-white legends, light blue accent, red LED 7-segment program number (lit and dim segments, drawn with
  rects), "OBERVAVE" in FONT_L with three blue bands through the lower half of the letters, section titles between
  thin rules like a silkscreen.
- 4 anti-aliased round knobs (coverage by distance, blended RGB565) with a blue value arc and a pointer, label
  above, value below. A scope strip (blue trace on near black). 8 round red voice LEDs glowing with each voice's
  amp envelope, tags for mode (POLY/DUAL/UNI/MONO), filter type, ARP, HOLD, FX.
- Splash at boot (~2.4 s): the logo. Redraw only what changed; scope + LEDs ~20-25 fps.
  Raise `CV_MAX` (canvas in .pool) for bigger strips.

## Style

As Dinghy / SLOOP: C99, 4 spaces, short comments that say why, unity build. Commit messages: one line what changed.
Commit after each tested step. Host test before every device build: listen to / look at `build/host/*`.
On GitHub: <https://github.com/zvenson/obervave> (public, since 2026-10-13). Push after each tested step.
