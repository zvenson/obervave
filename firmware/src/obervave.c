/* SPDX-License-Identifier: GPL-3.0-only */
/* OberVave 3.3: an analog-style polysynth for the M-VAVE FM-1 (and the SMK-37) in the spirit of the classic American
 * poly synths of 1979-84. Four tracks, each with its own sound, arpeggiator and sequence, sharing the eight voices; a
 * mixer (level, pan, four effect sends, mute, solo) with a MASTER (DJ filter, drive, compressor); the tracks start and
 * stop on the bar, as clips in Ableton Live; every pattern keeps its sounds and its mix. Grown from the VIBE edition
 * (OberVave 2.0 / 3.0 for VIBE FM Mk1) back onto the FM-1. Built on Dinghy's platform (platform.c).
 *
 *   obv_dsp.h       oscillators (polyBLEP events), filter (trapezoidal SVF), tanh
 *   obv_params.h    the parameter table: patches, pages, MIDI CC
 *   obv_fx.h        chorus, ping-pong delay, plate reverb (after the voices, same ISR)
 *   obv_engine.h    the voices, envelopes, LFO, arpeggiator: all in the audio ISR
 *   obv_seq.h       the step sequencer: four tracks of 256 steps, played in the ISR too
 *   obv_patches.h   the factory programs
 *   this file       the panel, MIDI, the screen, the storage
 *
 * Integers only (no FPU, no 64-bit division); tests/host.c runs this on a computer. */
static void app_idle(void);
#define LCD_IDLE() app_idle()                   /* (lcd.c: while the DMA runs) */
#define LCD_MIRROR 1                            /* (lcd.c: what is painted also goes to FM-1 Mirror – obv_mirror.h) */
#include "platform.c"
#include "obv_mirror.h"
#include "obv_dsp.h"
#include "obv_params.h"
#include "obv_fx.h"
#include "obv_engine.h"
#include "obv_seq.h"
#include "obv_patches.h"

static void app_audio(int32_t *out, uint32_t n)
{
#ifndef OBV_HOST
    uint32_t t0 = fm1_ticks();
#endif
    engine_audio(out, n);
#ifndef OBV_HOST
    ui_render_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
#endif
}

/* ---- what survives a power-off: the user programs and the globals, one settings sector */
#define NFXP (P_REV_MIX - P_CHO_RATE + 1u)     /* the effects' parameters */
#define SET_VERSION 7u                          /* 7: 3.0, four tracks and the mixer; 3: 1.5, the programs in their own sectors;
                                                   2: 1.4, 32 programs in here */
static struct {
    uint8_t version, prog, tempo, midi_ch;      /* midi_ch 0 = omni, else 1..16: track A's channel, B-D the next three */
    int8_t tune, oct;
    uint8_t pat, click;                         /* pat: the sequencer pattern (its own flash slot); click: CLICK_* */
    uint8_t tprog[NTRK];                        /* each track's program */
    uint8_t mix_lvl[NTRK];                      /* the mixer: level 0..127 (100 = unity) */
    uint8_t mix_cho[NTRK], mix_dly[NTRK], mix_rev[NTRK];   /* the sends into chorus, delay, reverb 0..127 */
    int8_t mix_pan[NTRK];                       /* -64..64 */
    uint8_t mix_mute, mix_solo, rec_q, sync_int;   /* mute, solo: bit t; rec_q: (unused); sync_int: 1 = SYNC INTERNAL
                                                 * (a MIDI clock, start and stop are ignored) */
    uint8_t fx[NFXP], fx_ok;                    /* the effects (chorus, delay, reverb): the song's, not a sound's; ok: FX_OK */
    uint8_t rec_qt[NTRK], rec_qt_ok;            /* REC's quantise per track; ok: RQT_OK */
    uint8_t mix_drv[NTRK], dv_type, dv_amt, dv_tone, dv_mix, dv_ok;   /* the drive send per track, the drive's type,
                                                   amount, tone, return; ok: DV_OK */
    uint8_t m_vol, m_dj, m_drive, m_comp, m_ok; /* MASTER: volume (100 = unity), DJ filter (64 = off, below LP, above
                                                   HP), drive, compressor; ok: M_OK */
} set;
#define MIDI_OUT_CH (set.midi_ch ? (set.midi_ch - 1u + seq_trk % NTRK) & 15u : 0u)   /* the keys go out on the track's MIDI channel (OMNI: 1) */
#define RQT_OK 0xA5u
#define M_OK 0x3Cu
#define DV_OK 0x5Du
static const char *const DV_NAME[NDV] = {"OVERDRV", "DIST", "FUZZ", "CRUSH"};
#define FX_OK 0x5Au
enum { RQ_OFF, RQ_32, RQ_16, RQ_8, RQ_4, NRQ };   /* REC: unquantised, or to 1/32 .. 1/4 */
static const char *const RQ_NAME[NRQ] = {"OFF", "1/32", "1/16", "1/8", "1/4"};
static const uint8_t RQ_PER_QUARTER[NRQ] = {0, 8, 4, 2, 1};
static const uint8_t TPROG_DEF[NTRK] = {0, 6, 42, 57};   /* DARK ARP, FAT BASS, WARM PAD, SOLO LEAD */
typedef struct {                                /* SET_VERSION 3 (1.5.x): read once, to bring the globals over */
    uint8_t version, prog, tempo, midi_ch;
    int8_t tune, oct;
    uint8_t pat, click;
} set_v3_t;
typedef struct {                                /* SET_VERSION 2 (1.4.3 and before): read once, to bring the programs over */
    uint8_t version, prog, tempo, midi_ch;
    int8_t tune, oct;
    uint8_t pat, click;
    uint32_t used;
    patch_t user[32];
} set_v2_t;
static uint32_t set_dirty, set_t;
/* the user's programs: PS_N to a flash sector (psect_load / psect_save), all of them in RAM */
#define PS_N 50u
#define PS_TAG 0x50564F42u                      /* "BOVP" */
typedef struct { uint32_t tag; uint8_t used[PS_N], pad[2]; patch_t p[PS_N]; } psect_t;
_Static_assert(sizeof(psect_t) <= SETTINGS_MAX, "a program sector outgrew its flash sector");
_Static_assert(NPSECT * PS_N >= NPROG, "too few program sectors");
static psect_t psect[NPSECT];
static uint32_t ps_dirty;                       /* bit i: program sector i to save */

static uint32_t page = PG_PERFORM, hold, save_armed, save_t, save_to;   /* save_to: SAVE's slot (PRESETS); hold: bit t = track t's HOLD */
static uint32_t trk_prog[NTRK], trk_edited[NTRK];   /* each track's program, and whether its sound was changed since */
static uint32_t mix_row;                        /* the mixer page's knobs: 0 LEVEL, 1 PAN, 2 CHORUS, 3 DELAY, 4 REVERB, 5 DRIVE, 6 MASTER */

/* the sequencer, as the panel sees it */
enum { REC_OFF, REC_READY, REC_ON };            /* REC: off, armed (the first note starts the loop), recording */
static uint32_t seq_trk, seq_cur[NTRK], seq_pg[NTRK], rec, pat_edited, pat_dirty, seq_back = PG_PERFORM;
static uint32_t rec_down, rec_down_t, beat_seen, beat_t;   /* rec_down: REC held since rec_down_t */
static uint32_t rec_wait;                       /* a track + 1 that REC waits for: it records from its start on the bar */
static struct { uint16_t step; uint32_t t; } rec_open[128];   /* a recorded note still held: where and when it began */
static pattern_t undo_pat __attribute__((section(".pool")));                      /* one level of undo: the pattern before the last pass / erase / clear */
static uint32_t undo_ok;
static uint8_t erase_on[128];                   /* notes erased while their key is held (EDIT + key, playing) */
static uint32_t erase_pos = 0xFFFFFFFFu;
static uint32_t pen = 60;                       /* the note a tapped empty step gets: the last one played */
static int32_t step_held = -1;                  /* the step whose white key is held in the step view, -1 none */
static uint32_t step_held_t, step_turned, step_midi, toast_t;
static uint8_t audition[SEQ_CHORD], audition_part;   /* the held step's notes sounding (stopped: you hear what you hold) */
static const char *toast;
static int is_seq_page(uint32_t pg) { return pg == PG_SEQ; }
/* the full-screen sequencer's views (SEQ tapped steps through the first three; SAVE tapped there: PATTERNS) */
enum { SV_TRACKS, SV_STEPS, SV_MORE, SV_TRACK, SV_SLOTS, NSV };   /* TRACKS first: A-D one under the other (SLOOP) */
static const char *const SV_NAME[NSV] = {"TRACKS", "STEPS", "STEP+", "TRACK", "PATTERNS"};
#define SV_ENTRY(v) ((v) == SV_STEPS || (v) == SV_MORE)   /* the views where the keys write into the cursor's step */
/* STEP+ is no view of its own: SELECT held down in STEPS shows its knobs (RATCHET, CHANCE, ON/OFF), as VIBE's SHIFT */
static uint32_t more_held;
#define SV_EFF (seq_view == SV_STEPS && more_held ? (uint32_t)SV_MORE : seq_view)
static uint32_t seq_view, slot_sel, pat_used, pat_next, arm_k, arm_t, entry_n, pat_edit_t, pend_slot, pat_save_say;
static uint8_t entry_on_note[128];              /* the keys writing into the cursor step now */
static uint16_t slot_len[NBLOB][NTRK];          /* the saved patterns' track lengths (the PATTERNS view) */
static uint32_t pat_save_armed, pat_save_t, q_edited;   /* q_edited: the next pattern brings unsaved edits */    /* SAVE tapped in the sequencer: SAVE PATTERN? (again: saved) */
static uint32_t pat_queue, copy_to, copy_arm, copy_arm_t;   /* pat_queue: the next pattern + 1 (playing: on the next bar) */
enum { CLICK_OFF, CLICK_REC, CLICK_ON, CLICK_COUNT, NCLICK };   /* COUNT: as REC, and REC armed + PLAY counts in a bar */
static const char *const CLICK_NAME[NCLICK] = {"OFF", "REC", "ON", "COUNT-IN"};
/* the panel as sloopDX's: a function button tapped (let go, nothing else touched) opens its pages; held, it is a
 * layer (the keys and knobs change job until it is let go; HOME tapped while it is held locks it open) */
enum { L_NONE, L_SEQ, L_EDIT, L_GLO, L_SAVE, L_FX };
static uint32_t layer, layer_lock, layer_used, layer_t, layer_btn, rec_from_arp;
#if BOARD_SMK37
/* the SMK-37's step pads (obv_smk.h): held pads, the velocity a new step gets; a pad held 250 ms shows its step */
static uint32_t smk_pads_held, smk_pad_t0, smk_step_vel;
#define SMK_PAD_HELD (smk_pads_held != 0u && fm1_ms - smk_pad_t0 >= 250u)
#define KEY_VEL(k) fm1_in.key_vel[SMK_KEY_F3 + (k)]   /* the keys play with velocity (the unit's factory curves) */
#define STEP_NEW_VEL(st) do { if (smk_step_vel) (st)->vel = (uint8_t)smk_step_vel; } while (0)
#define REC_TIE_VEL(t, s0) seq_step(t, s0)->vel   /* a recorded long note: its ties keep the played velocity */
/* the button and knob names in the hints: the SMK's printed ones (PARA = EDIT, PATCH = HOME, SCALE = SEL; K2 SELECT,
 * K3 ALGORITHM, K8 = KNOB 4) */
#define NM_EDIT "PARA"
#define NM_HOME "PATCH"
#define NM_SELBTN "SCALE"
#define NM_SELECT "K2"
#define NM_ALGO "K3"
#define NM_KNOB4 "K8"
#define NM_K1 "K5"                              /* (the short names in the sequencer's lines) */
#define NM_K3 "K7"
#define NM_K4 "K8"
#define NM_PRESETS "K4"
#define NM_SAVE "BT"
#else
#define SMK_PAD_HELD 0
#define KEY_VEL(k) 100
#define STEP_NEW_VEL(st) ((void)0)
#define REC_TIE_VEL(t, s0) 100u
#define NM_EDIT "EDIT"
#define NM_HOME "HOME"
#define NM_SELBTN "SELECT"
#define NM_SELECT "SELECT"
#define NM_ALGO "ALGORITHM"
#define NM_KNOB4 "KNOB 4"
#define NM_K1 "K1"
#define NM_K3 "K3"
#define NM_K4 "K4"
#define NM_PRESETS "PRESETS"
#define NM_SAVE "SAVE"
#endif
static int steps_on(void) { return layer == L_SEQ && !rec; }
static int seq_shown(void) { return is_seq_page(page) || layer == L_SEQ || layer == L_EDIT || SMK_PAD_HELD; }
static uint8_t pen_chord[4];                    /* the last note or chord played: what an empty step gets */
static uint32_t pen_n, notes_down, tap_t[3];
static void say(const char *s) { toast = s; toast_t = fm1_ms; }
static void fmt_u(char *s, uint32_t v);
static uint32_t slen(const char *s);
static void scat(char *d, const char *s);

/* the step view: the 16 white keys are steps 1-16 of the page, the black keys are the sequencer's functions */
static const int8_t KEY_STEP[27] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
static const int8_t KEY_PAGE[27] = {[0] = -1, [1] = 0, [2] = -1, [3] = 1, [4] = -1, [5] = 2, [6] = -1, [7] = -1, [8] = 3,
                                    [9] = -1, [10] = 4, [11] = -1, [12] = -1, [13] = 5, [14] = -1, [15] = 6, [16] = -1,
                                    [17] = 7, [18] = -1, [19] = -1, [20] = -1, [21] = -1, [22] = -1, [23] = -1, [24] = -1,
                                    [25] = -1, [26] = -1};   /* the SEQ layer: black keys F#3 .. A#4 = page 1 .. 8 */
static const char *const LEVEL_NAME[4] = {"GHOST", "SOFT", "NORM", "HARD"};
static const uint8_t LEVEL_VEL[4] = {40, 70, 100, 127};
static uint32_t level_of(uint32_t vel) { return vel <= 50u ? 0u : vel <= 85u ? 1u : vel <= 113u ? 2u : 3u; }
static uint32_t seq_pages(uint32_t t) { return pat.trk[t].len ? (pat.trk[t].len + 15u) / 16u : 1u; }
static uint8_t cc_map[128];                     /* MIDI CC -> parameter + 1, 0 = none */

static void prog_get(uint32_t n, patch_t *p)    /* program n: the user's, else the factory's (65-70: VIBE's, 71-99: INIT) */
{
    n %= NPROG;
    if (psect[n / PS_N].used[n % PS_N]) *p = psect[n / PS_N].p[n % PS_N];
    else patch_from_factory(p, n < NFACT ? &FACTORY[n] : n < NFACT + NVIBE ? &FACT_VIBE[n - NFACT] : &FACT_INIT);
}

static void prog_put(uint32_t n, const patch_t *p)   /* program n is the user's now; saved when quiet */
{
    n %= NPROG;
    psect[n / PS_N].p[n % PS_N] = *p;
    psect[n / PS_N].used[n % PS_N] = 1;
    ps_dirty |= 1u << (n / PS_N);
    set_t = fm1_ms;
}

/* four sounds, one per track: the panel is on the chosen track's (ALGORITHM, the pads' last row): the keys play it,
 * the pages edit it, PRESETS picks it, SAVE saves it. The effects are one chain: track A's sound sets them. */
static const char TRK_L[NTRK + 1] = "ABCD";
/* the effects are global (as a DAW's return tracks): a sound on track A brings no effects with it and takes none away,
 * so a program change, a dice or a load on A never cuts the reverb's tail. They live in track A's patch (the FX page
 * and the engine read them there) and are kept in set.fx */
static void fx_keep(patch_t *p) { uint32_t i; if (set.fx_ok == FX_OK) for (i = 0; i < NFXP; i++) p->v[P_CHO_RATE + i] = set.fx[i]; }
static void fx_sync(void)                       /* the effects as edited (FX page, MIDI CC) into the settings */
{
    uint32_t i, ch = set.fx_ok != FX_OK;
    for (i = 0; i < NFXP; i++) if (set.fx[i] != live_p[0].v[P_CHO_RATE + i]) { set.fx[i] = live_p[0].v[P_CHO_RATE + i]; ch = 1; }
    if (ch) { set.fx_ok = FX_OK; set_dirty = 1; set_t = fm1_ms; }
}
static int is_a_only(uint32_t p) { return p >= P_CHO_RATE && p <= P_REV_MIX; }   /* (the effects: one chain; each sound has its arp) */
static patch_t *ed_patch(uint32_t p) { return is_a_only(p) ? &live_p[0] : &live_p[seq_trk % NTRK]; }
static uint32_t ed_prog(void) { return trk_prog[seq_trk % NTRK]; }
static uint32_t ed_edited(void) { return trk_edited[seq_trk % NTRK]; }

static void trk_load(uint32_t t, uint32_t n)    /* track t: program n */
{
    patch_t p;
    t %= NTRK; n %= NPROG;
    prog_get(n, &p);
    if (!t) fx_keep(&p);                        /* (track A: the effects stay) */
    live_p[t] = p;
    trk_prog[t] = n;
    trk_edited[t] = 0;
    set.tprog[t] = (uint8_t)n;
    if (!t) set.prog = (uint8_t)n;
    set_dirty = 1; set_t = fm1_ms;
}

/* the panel's notes (keys, pads, MIDI) go to the sound the panel is on; a release goes where its note began */
static uint8_t note_part[128];
static void play_note(uint32_t on, uint32_t note, uint32_t vel)
{
    note &= 127u;
    if (on) note_part[note] = (uint8_t)(seq_trk % NTRK);
    note_event_p(note_part[note], on, note, vel);
}
static int rec_keys(void) { return !ed_patch(P_ARP_MODE)->v[P_ARP_MODE]; }   /* (feeding an arp: its notes are recorded) */

/* ---- the mixer: the settings (saved) into the engine; mute and solo as one mask of silent tracks */
static void mix_apply(void)
{
    uint32_t t, solo = set.mix_solo & ((1u << NTRK) - 1u);
    for (t = 0; t < NTRK; t++) {
        ctl_mix_lvl[t] = (uint8_t)(set.mix_lvl[t] > 127u ? 127u : set.mix_lvl[t]);
        ctl_mix_pan[t] = (int8_t)clampi(set.mix_pan[t], -64, 64);
        ctl_mix_cho[t] = (uint8_t)(set.mix_cho[t] > 127u ? 127u : set.mix_cho[t]);
        ctl_mix_dly[t] = (uint8_t)(set.mix_dly[t] > 127u ? 127u : set.mix_dly[t]);
        ctl_mix_rev[t] = (uint8_t)(set.mix_rev[t] > 127u ? 127u : set.mix_rev[t]);
        ctl_mix_drv[t] = (uint8_t)(set.mix_drv[t] > 127u ? 127u : set.mix_drv[t]);
    }
    ctl_mix_off = solo ? ~solo & ((1u << NTRK) - 1u) : set.mix_mute & ((1u << NTRK) - 1u);
    ctl_dv_type = set.dv_type; ctl_dv_amt = set.dv_amt; ctl_dv_tone = set.dv_tone; ctl_dv_mix = set.dv_mix;   /* (VIBE) */
}
static uint32_t trk_silent(uint32_t t) { return ctl_mix_off >> (t % NTRK) & 1u; }

/* ---- every pattern keeps its instruments and its mix (as VIBE's 3.0): the four tracks' sounds with their edits and
 * program numbers, the mixer (level, pan, the four sends, mute, solo), the effects and MASTER. They live behind the
 * pattern's steps in its flash slot, all eight sets in RAM; a pattern without its own set takes what is playing */
#define PSN2_MAGIC 0x324E5350u                  /* "PSN2" */
typedef struct {
    uint8_t lvl[NTRK], cho[NTRK], dly[NTRK], rev[NTRK], drv[NTRK]; int8_t pan[NTRK];
    uint8_t mute, solo, fx[NFXP], dv_type, dv_amt, dv_tone, dv_mix, m_vol, m_dj, m_drive, m_comp;
} mixset_t;
typedef struct { uint32_t magic; uint8_t prog[NTRK], edited[NTRK]; patch_t p[NTRK]; mixset_t mx; } psnd_t;
static psnd_t psnd[NPAT], psnd_tmp;
static uint32_t snd_dirty, snd_t, snd_chg_t;    /* bit n: pattern n's set changed (its slot to save); snd_t: the last look;
                                                 * snd_chg_t: the last change */
static int bytes_same(const void *a, const void *b, uint32_t n) { const uint8_t *x = a, *y = b; while (n--) if (*x++ != *y++) return 0; return 1; }
static void mx_from_set(mixset_t *m)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) { m->lvl[t] = set.mix_lvl[t]; m->cho[t] = set.mix_cho[t]; m->dly[t] = set.mix_dly[t]; m->rev[t] = set.mix_rev[t]; m->drv[t] = set.mix_drv[t]; m->pan[t] = set.mix_pan[t]; }
    m->mute = set.mix_mute; m->solo = set.mix_solo;
    for (i = 0; i < NFXP; i++) m->fx[i] = set.fx[i];
    m->dv_type = set.dv_type; m->dv_amt = set.dv_amt; m->dv_tone = set.dv_tone; m->dv_mix = set.dv_mix;
    m->m_vol = set.m_vol; m->m_dj = set.m_dj; m->m_drive = set.m_drive; m->m_comp = set.m_comp;
}
static void mx_to_set(const mixset_t *m)        /* into the settings and the engine */
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) { set.mix_lvl[t] = m->lvl[t]; set.mix_cho[t] = m->cho[t]; set.mix_dly[t] = m->dly[t]; set.mix_rev[t] = m->rev[t]; set.mix_drv[t] = m->drv[t]; set.mix_pan[t] = m->pan[t]; }
    set.mix_mute = m->mute; set.mix_solo = m->solo;
    for (i = 0; i < NFXP; i++) set.fx[i] = m->fx[i];
    set.fx_ok = FX_OK;
    set.dv_type = m->dv_type; set.dv_amt = m->dv_amt; set.dv_tone = m->dv_tone; set.dv_mix = m->dv_mix;
    set.m_vol = m->m_vol; set.m_dj = m->m_dj; set.m_drive = m->m_drive; set.m_comp = m->m_comp;
    mix_apply();
}
static void mx_prepare(uint32_t n)              /* pattern n's mixer, drive, master as the ISR takes them (mx_q) */
{
    const psnd_t *s = &psnd[n % NPAT];
    uint32_t t, solo, mute;
    if (s->magic != PSN2_MAGIC) {               /* (no own set: the mix stays as it is) */
        for (t = 0; t < NTRK; t++) { mx_q.lvl[t] = ctl_mix_lvl[t]; mx_q.cho[t] = ctl_mix_cho[t]; mx_q.dly[t] = ctl_mix_dly[t]; mx_q.rev[t] = ctl_mix_rev[t]; mx_q.drv[t] = ctl_mix_drv[t]; mx_q.pan[t] = ctl_mix_pan[t]; }
        mx_q.off = ctl_mix_off; mx_q.dv_type = ctl_dv_type; mx_q.dv_amt = ctl_dv_amt; mx_q.dv_tone = ctl_dv_tone; mx_q.dv_mix = ctl_dv_mix;
        mx_q.m_vol = ctl_m_vol; mx_q.m_dj = ctl_m_dj; mx_q.m_drive = ctl_m_drive; mx_q.m_comp = ctl_m_comp;
        return;
    }
    for (t = 0; t < NTRK; t++) {
        mx_q.lvl[t] = s->mx.lvl[t] > 127u ? 127u : s->mx.lvl[t]; mx_q.pan[t] = (int8_t)clampi(s->mx.pan[t], -64, 64);
        mx_q.cho[t] = s->mx.cho[t] > 127u ? 127u : s->mx.cho[t]; mx_q.dly[t] = s->mx.dly[t] > 127u ? 127u : s->mx.dly[t];
        mx_q.rev[t] = s->mx.rev[t] > 127u ? 127u : s->mx.rev[t]; mx_q.drv[t] = s->mx.drv[t] > 127u ? 127u : s->mx.drv[t];
    }
    solo = s->mx.solo & ((1u << NTRK) - 1u); mute = s->mx.mute & ((1u << NTRK) - 1u);
    mx_q.off = solo ? ~solo & ((1u << NTRK) - 1u) : mute;
    mx_q.dv_type = s->mx.dv_type; mx_q.dv_amt = s->mx.dv_amt; mx_q.dv_tone = s->mx.dv_tone; mx_q.dv_mix = s->mx.dv_mix;
    mx_q.m_vol = s->mx.m_vol; mx_q.m_dj = s->mx.m_dj; mx_q.m_drive = s->mx.m_drive; mx_q.m_comp = s->mx.m_comp;
}
static void snd_capture(uint32_t n)             /* what plays now into pattern n's set */
{
    psnd_t *s = &psnd[n % NPAT];
    uint32_t t;
    s->magic = PSN2_MAGIC;
    for (t = 0; t < NTRK; t++) { s->p[t] = live_p[t]; s->prog[t] = (uint8_t)trk_prog[t]; s->edited[t] = (uint8_t)trk_edited[t]; }
    mx_from_set(&s->mx);
    snd_dirty |= 1u << (n % NPAT);
    snd_chg_t = fm1_ms;
}
static void snd_target(uint32_t n, patch_t *out)   /* pattern n's four sounds (its effects in track A's, where the engine reads them) */
{
    const psnd_t *s = &psnd[n % NPAT];
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) out[t] = s->magic == PSN2_MAGIC ? s->p[t] : live_p[t];
    for (i = 0; i < NFXP; i++) out[0].v[P_CHO_RATE + i] = s->magic == PSN2_MAGIC ? s->mx.fx[i] : set.fx[i];
}
static void snd_meta(uint32_t n)                /* pattern n's program numbers and edit marks, its mixer, effects, master */
{
    const psnd_t *s = &psnd[n % NPAT];
    uint32_t t;
    if (s->magic != PSN2_MAGIC) { snd_capture(n); return; }
    for (t = 0; t < NTRK; t++) { trk_prog[t] = s->prog[t] % NPROG; trk_edited[t] = s->edited[t]; set.tprog[t] = (uint8_t)trk_prog[t]; }
    set.prog = set.tprog[0];
    mx_to_set(&s->mx);
    set_dirty = 1; set_t = fm1_ms;
}
static int snd_changed(uint32_t n)              /* the sounds and the mix now differ from pattern n's set */
{
    const psnd_t *s = &psnd[n % NPAT];
    mixset_t now;
    uint32_t t;
    if (s->magic != PSN2_MAGIC) return 1;
    for (t = 0; t < NTRK; t++)
        if (s->prog[t] != trk_prog[t] || s->edited[t] != trk_edited[t] || !bytes_same(&s->p[t], &live_p[t], sizeof(patch_t))) return 1;
    mx_from_set(&now);
    return !bytes_same(&s->mx, &now, sizeof now);
}

/* ---- the patterns of 1.5 (two tracks sharing 256 steps, one sector each): read once to bring them over. A plays the
 * program of 1.5, B its own (or A's), both into the effects as before; C and D are off */
#define PAT1_MAGIC 0x31514553u                  /* "SEQ1" */
typedef struct { uint8_t note[SEQ_CHORD], vel, gate, ratch, chance; } step1_t;
typedef struct { uint32_t magic; seqtrk_t trk[2]; step1_t st[SEQ_STEPS]; } pattern1_t;
static uint32_t v1_snd;                         /* bit n: psnd[n] was made from 1.5's pattern n already */
static int pat_from_v1(uint32_t n, pattern_t *p)
{
    static pattern1_t o __attribute__((section(".pool")));
    uint32_t i, t, bp, bdry;
    n %= NPAT;
    if (blob_load(n, &o, sizeof o) || o.magic != PAT1_MAGIC) return -1;
    pat_init(p);
    for (t = 0; t < 2u; t++) {
        uint32_t len = o.trk[t].len > SEQ_STEPS ? SEQ_STEPS : o.trk[t].len;
        p->trk[t] = o.trk[t];
        p->trk[t].len = (uint16_t)len; p->trk[t].mute = 0; p->trk[t].pad[0] = p->trk[t].pad[1] = 0;
        for (i = 0; i < len; i++) {             /* (A from the front, B from the back) */
            const step1_t *a = &o.st[t ? SEQ_STEPS - 1u - i : i];
            step_t *d = &p->st[t][i];
            memcpy(d->note, a->note, SEQ_CHORD);
            d->vel = a->vel; d->gate = a->gate; d->ratch = a->ratch; d->chance = a->chance; d->nudge = 0;
        }
    }
    p->trk[2].len = p->trk[3].len = 0;
    if (v1_snd >> n & 1u) return 0;
    v1_snd |= 1u << n;
    bp = o.trk[1].pad[0]; bdry = o.trk[1].pad[1] > 127u ? 127u : o.trk[1].pad[1];
    {
        psnd_t *s = &psnd[n];
        s->magic = PSN2_MAGIC;
        for (t = 0; t < NTRK; t++) {
            uint32_t pr = t == 0u ? set.tprog[0] : t == 1u ? (bp && bp <= NPROG ? bp - 1u : set.tprog[0]) : TPROG_DEF[t];
            s->prog[t] = (uint8_t)pr; s->edited[t] = 0;
            prog_get(pr, &s->p[t]);
        }
        mx_from_set(&s->mx);
        for (i = 0; i < NFXP; i++) s->mx.fx[i] = s->p[0].v[P_CHO_RATE + i];   /* (A's program's effects, as 1.5 played them) */
        s->mx.cho[0] = s->mx.dly[0] = s->mx.rev[0] = 127;
        s->mx.cho[1] = s->mx.dly[1] = s->mx.rev[1] = (uint8_t)(bp ? 127u - bdry : 127u);
        s->mx.mute = (uint8_t)((o.trk[0].mute ? 1u : 0u) | (o.trk[1].mute ? 2u : 0u));
        s->mx.solo = 0;
    }
    return 0;
}

static void pat_fetch(uint32_t n, pattern_t *p)   /* pattern n: its slot, else 1.5's, else empty (its sounds stay in psnd[]) */
{
    n %= NPAT;
    if (!slot_load(n, p, sizeof *p, &psnd_tmp, sizeof psnd_tmp) && p->magic == PAT_MAGIC) {
        if (p->auto_magic != AUTO_MAGIC) auto_init(p);
        return;
    }
    if (!pat_from_v1(n, p)) return;
    pat_factory(n, p);
}
static void load_pattern(uint32_t n)            /* stopped: pattern n with its sounds and its mix */
{
    uint32_t t;
    n %= NPAT;
    ctl_panic = 1;                              /* (what the old one held lets go) */
    pat_fetch(n, &pat);
    set.pat = (uint8_t)n;
    if (psnd[n].magic == PSN2_MAGIC) {
        patch_t nx[NTRK];
        snd_target(n, nx);
        for (t = 0; t < NTRK; t++) live_p[t] = nx[t];
    }
    snd_meta(n);
    memset(seq_cur, 0, sizeof seq_cur); memset(seq_pg, 0, sizeof seq_pg);
    pat_edited = 0;
    pat_edit_t = 0;
}

static void seq_cursor_set(uint32_t t, uint32_t n)   /* the cursor, and the roll's 16 with it */
{
    seq_cur[t] = n;
    seq_pg[t] = n / 16u;
}

static void undo_save(void) { undo_pat = pat; undo_ok = 1; }

/* where now is on track t, as you heard it (the ~12 ms from a key to its sound taken back), in steps Q8, put on REC's
 * grid: 1/32 .. 1/4 (as many steps of the track as that is; finer than a step: a nudge into it), or OFF: as played.
 * An arp note: where the ISR played it (rec_arp_q8), never moved */
static uint32_t rec_arp_q8 = 0xFFFFFFFFu;
static uint32_t rec_where(uint32_t t, uint32_t *nudge)
{
    uint32_t len = pat.trk[t].len, dur = ui_seq_dur[t] ? ui_seq_dur[t] : 1u, back = 529u * 256u / dur, rq = set.rec_qt[t % NTRK] % NRQ;
    uint32_t q8 = ui_seq_pos[t] * 256u + (ui_seq_frac[t] > 256u ? 256u : ui_seq_frac[t]), g8, snapw;
    if (rec_from_arp) {
        rq = RQ_OFF;
        if (rec_arp_q8 != 0xFFFFFFFFu) q8 = rec_arp_q8 % (len * 256u);
    } else q8 = q8 >= back ? q8 - back : q8 + len * 256u - back;
    if (rq != RQ_OFF) {                         /* the grid in steps Q8: the track's steps per quarter / REC's */
        g8 = 256u * DIV_PER_QUARTER[pat.trk[t].div % NDIV] / RQ_PER_QUARTER[rq];
        if (!g8) g8 = 1;
        q8 = (q8 + g8 / 2u) / g8 * g8;
    }
    snapw = rec_from_arp ? 24u : 3u;            /* (on a step's line, an arp note or a key within ~1 ms: on it) */
    if ((q8 & 255u) < snapw) q8 &= ~255u;
    else if ((q8 & 255u) > 255u - snapw) q8 = (q8 | 255u) + 1u;
    q8 %= len * 256u;
    *nudge = (q8 & 255u) >> 1;                  /* (0..127: how far into the step) */
    return q8 >> 8;
}

static void step_add(step_t *st, uint32_t note, uint32_t vel)   /* a note into a step, the ones there stay */
{
    uint32_t k;
    if (!st->note[0]) { step_clear(st); st->gate = 96; }   /* (an empty step: on the step, until a recording nudges it) */
    for (k = 0; k < SEQ_CHORD && st->note[k] && st->note[k] != note; k++) ;
    if (k < SEQ_CHORD && !st->note[k]) st->note[k] = (uint8_t)note;
    st->vel = (uint8_t)vel;
    st->ratch &= (uint8_t)~STEP_OFF;
}

/* the steps where this recording started a note (a legato note ends there, not into it) */
static uint32_t rec_on_bits[NTRK][SEQ_STEPS / 32u];
static void rec_begin(void) { rec = REC_ON; undo_save(); memset(rec_on_bits, 0, sizeof rec_on_bits); }
#define REC_ON_AT(t, s) (rec_on_bits[(t) % NTRK][((s) % SEQ_STEPS) >> 5] >> ((s) & 31u) & 1u)

/* an arp take: from its first note while the keys (or HOLD) keep the arp going. When the loop comes round, what it
 * writes replaces what it wrote the round before (else every step got a second note and the take turned into chords);
 * the track's old steps are not played meanwhile (ctl_rec_mute) */
static uint32_t take_on, take_trk, take_pass, take_last;
static uint8_t take_mark[SEQ_STEPS];            /* the round (1..255) that wrote each step, 0 = not in this take */
static void take_step(uint32_t t, uint32_t s0, step_t *st)
{
    if (!take_on || take_trk != t) { take_on = 1; take_trk = t; take_pass = 1; take_last = s0; memset(take_mark, 0, sizeof take_mark); ctl_rec_mute = 1u << t; }
    if (s0 < take_last) take_pass = take_pass >= 255u ? 1u : take_pass + 1u;   /* (round again) */
    take_last = s0;
    if (take_mark[s0 % SEQ_STEPS] && take_mark[s0 % SEQ_STEPS] != take_pass) st->note[0] = 0;   /* (step_add: anew) */
    take_mark[s0 % SEQ_STEPS] = (uint8_t)take_pass;
}
static void take_check(void)                    /* the take is over: keys up and no HOLD, or REC off */
{
    if (take_on && (rec != REC_ON || (!notes_down && !(hold >> take_trk & 1u)) || !live_p[take_trk % NTRK].v[P_ARP_MODE])) { take_on = 0; ctl_rec_mute = 0; }
}

/* REC: a note (key, MIDI or the arp). Armed, the first note starts the loop on step 1. Recording, it lands where it was
 * heard, on top of what is there; when it is let go its length is written: a fraction of the step, or ties on, up to
 * the next note this recording started (legato) */
static void rec_note(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t t = seq_trk % NTRK, len = pat.trk[t].len, s0 = 0, n, k, held, sms, nudge = 0, sounding;
    note &= 127u; sounding = note;
    if (!len) return;
    if (ctl_count || rec_wait) return;          /* counting in, waiting for the bar: notes only sound */
    if (on && rec == REC_READY && !ctl_seq_run) {   /* the first note: from the top */
        ctl_seq_restart = 1; ctl_seq_run = 1;
        rec_begin();
    } else if (rec != REC_ON)
        return;
    else if (on)
        s0 = ctl_seq_restart ? 0u : rec_where(t, &nudge);
    if (on) {
        step_t *st = seq_step(t, s0);
        uint32_t was;
        if (rec_from_arp) take_step(t, s0, st);
        was = st->note[0];
        note = (uint32_t)clampi((int32_t)note - pat.trk[t].transp, 1, 127);   /* stored as heard */
        step_add(st, note, vel);
        if (!was) st->nudge = (uint8_t)nudge;   /* (a chord keeps its first note's timing) */
        rec_open[note].step = (uint16_t)s0;
        rec_open[note].t = fm1_ms + 1u;         /* (+1: 0 means none) */
        rec_on_bits[t][(s0 % SEQ_STEPS) >> 5] |= 1u << (s0 & 31u);
        if (!rec_from_arp) ctl_live_hold[t][sounding >> 5] |= 1u << (sounding & 31u);   /* (the key owns it now) */
        pat_edited = 1;
        return;
    }
    note = (uint32_t)clampi((int32_t)note - pat.trk[t].transp, 1, 127);
    if (!rec_open[note].t) return;
    held = fm1_ms + 1u - rec_open[note].t;
    s0 = rec_open[note].step;
    rec_open[note].t = 0;
    sms = ui_seq_dur[t] ? ui_seq_dur[t] * 10u / 441u : 125u;   /* a step in ms */
    if (!sms) sms = 1;
    n = (held + sms / 2u) / sms;                /* the steps it spans */
    if (n > len) n = len;
    for (k = 1; k < n; k++)                     /* legato: up to the next step where this recording started a note */
        if (REC_ON_AT(t, (s0 + k) % len)) {
            if (k == 1u) { seq_step(t, s0)->gate = 120; return; }
            n = k;
            break;
        }
    if (n <= 1u) {
        seq_step(t, s0)->gate = (uint8_t)clampi((int32_t)(held * 127u / sms), 24, 120);
        return;
    }
    for (k = 0; k < n; k++) {                   /* tied on: the note in each step it lasts, the last one ends */
        step_t *st = seq_step(t, (s0 + k) % len);
        step_add(st, note, vel ? vel : REC_TIE_VEL(t, s0));
        st->gate = k + 1u < n ? GATE_TIE : 110;
    }
}

/* a knob turned while REC records (the sequencer playing, the track on): its value onto the step the track plays now, in
 * the lane of that parameter (a new one if there is none yet, NLANE per track). While it turns, the player leaves the
 * lane to the hand (ctl_auto_hold, let go 250 ms after the last turn: auto_poll) */
static uint32_t auto_hold_t[NTRK][NLANE];
static void auto_rec(uint32_t p)
{
    static char m[32];
    uint32_t t = seq_trk % NTRK, l, f = NLANE, len = pat.trk[t].len;
    if (rec != REC_ON || !ctl_seq_run || !len || !(ctl_trk_on >> t & 1u) || is_a_only(p) || p >= NPARAM) return;
    for (l = 0; l < NLANE; l++) { if (pat.lane[t][l].param == p) break; if (f == NLANE && pat.lane[t][l].param == AUTO_NONE) f = l; }
    if (l == NLANE) {
        if (f == NLANE) { m[0] = 0; scat(m, "TRACK "); m[6] = TRK_L[t]; m[7] = 0; scat(m, ": 4 KNOBS RECORDED"); say(m); return; }
        l = f; lane_clear(&pat.lane[t][l]); pat.lane[t][l].param = (uint8_t)p;
    }
    pat.lane[t][l].v[ui_seq_pos[t] % len] = live_p[t].v[p];
    ctl_auto_hold[t] |= 1u << l; auto_hold_t[t][l] = fm1_ms;
    pat_edited = 1;
}
static void auto_poll(void)                     /* (main loop) a lane let go: the player has it again */
{
    uint32_t t, l;
    for (t = 0; t < NTRK; t++)
        for (l = 0; l < NLANE; l++)
            if ((ctl_auto_hold[t] >> l & 1u) && fm1_ms - auto_hold_t[t][l] > 250u) ctl_auto_hold[t] &= ~(1u << l);
}

/* EDIT held + a key: that note leaves the pattern (stopped: everywhere at once; playing: where the playhead passes
 * while the key is held, as an MPC) */
static void step_remove(step_t *st, uint32_t note)
{
    uint32_t k, j;
    for (k = j = 0; k < SEQ_CHORD; k++)
        if (st->note[k] && st->note[k] != note) st->note[j++] = st->note[k];
    for (; j < SEQ_CHORD; j++) st->note[j] = 0;
}
static void erase_note(uint32_t note)
{
    uint32_t t = seq_trk, n;
    note = (uint32_t)clampi((int32_t)note - pat.trk[t].transp, 1, 127);
    if (ctl_seq_run) { erase_on[note] = 1; step_remove(seq_step(t, ui_seq_pos[t] % (pat.trk[t].len ? pat.trk[t].len : 1u)), note); }
    else for (n = 0; n < pat.trk[t].len; n++) step_remove(seq_step(t, n), note);
    pat_edited = 1;
}

static void cc_map_init(void)
{
    uint32_t i, next = 20;
    for (i = 0; i < NPARAM; i++)
        if (PDEF[i].cc) cc_map[PDEF[i].cc] = (uint8_t)(i + 1u);
    for (i = 0; i < NPARAM; i++) {
        if (PDEF[i].cc) continue;
        while (next < 120u && (cc_map[next] || next == 64u)) next++;
        if (next < 120u) cc_map[next++] = (uint8_t)(i + 1u);
    }
}

static void set_param_in(patch_t *d, uint32_t p, int32_t v)
{
    v = clampi(v, 0, PDEF[p].max);
    if (d->v[p] == v) return;
    d->v[p] = (uint8_t)v;
    trk_edited[(uint32_t)(d - live_p) % NTRK] = 1;
}
static void set_param(uint32_t p, int32_t v) { set_param_in(ed_patch(p), p, v); }

/* REC: a new sound within useful ranges */
static uint32_t rnd;
static uint32_t rn(uint32_t lo, uint32_t hi) { return lo + xorshift(&rnd) % (hi - lo + 1u); }
static void randomize(void)
{
    uint32_t i, r;
    patch_t *d = &live_p[seq_trk % NTRK];
    for (i = 0; i < NPARAM; i++) d->v[i] = PDEF[i].def;
    d->v[P_O1_WAVE] = (uint8_t)rn(1, 3);
    d->v[P_O2_WAVE] = (uint8_t)rn(0, 3);
    d->v[P_O1_PW] = (uint8_t)rn(10, 110);
    d->v[P_O2_PW] = (uint8_t)rn(10, 110);
    d->v[P_O2_LVL] = (uint8_t)rn(60, 127);
    r = rn(0, 9);
    d->v[P_O2_SEMI] = (uint8_t)(r < 4 ? 24 : r < 6 ? 12 : r < 8 ? 36 : r == 8 ? 31 : 43);
    d->v[P_O2_FINE] = (uint8_t)rn(60, 76);
    d->v[P_NOISE] = (uint8_t)(rn(0, 5) ? 0 : rn(10, 70));
    d->v[P_SYNC] = (uint8_t)(rn(0, 4) == 0);
    d->v[P_XMOD] = (uint8_t)(rn(0, 4) ? 0 : rn(20, 100));
    d->v[P_ENV_O2] = (uint8_t)(d->v[P_SYNC] ? rn(70, 110) : 64);
    d->v[P_CUTOFF] = (uint8_t)rn(30, 110);
    d->v[P_RESO] = (uint8_t)rn(0, 90);
    d->v[P_FENV_AMT] = (uint8_t)rn(60, 120);
    d->v[P_FTYPE] = (uint8_t)(rn(0, 2) ? F_LP24 : rn(0, NFTYPE - 1));
    d->v[P_FMORPH] = (uint8_t)rn(0, 127);
    d->v[P_DRIVE] = (uint8_t)(rn(0, 2) ? 0 : rn(10, 70));
    d->v[P_FA] = (uint8_t)rn(0, 90); d->v[P_FD] = (uint8_t)rn(30, 100); d->v[P_FS] = (uint8_t)rn(0, 100); d->v[P_FR] = (uint8_t)rn(20, 100);
    d->v[P_AA] = (uint8_t)rn(0, 80); d->v[P_AD] = (uint8_t)rn(30, 100); d->v[P_AS] = (uint8_t)rn(0, 127); d->v[P_AR] = (uint8_t)rn(20, 100);
    d->v[P_LFO_SHAPE] = (uint8_t)rn(0, 5);
    d->v[P_LFO_RATE] = (uint8_t)rn(20, 100);
    d->v[P_LFO_PITCH] = (uint8_t)(rn(0, 2) ? 0 : rn(5, 40));
    d->v[P_LFO_PWM] = (uint8_t)(rn(0, 1) ? 0 : rn(20, 90));
    d->v[P_LFO_FLT] = (uint8_t)(rn(0, 2) ? 0 : rn(10, 70));
    d->v[P_LFO_DELAY] = (uint8_t)rn(0, 70);
    d->v[P_LFO_PHASE] = (uint8_t)rn(0, 2);
    r = rn(0, 9);
    d->v[P_MODE] = (uint8_t)(r < 6 ? M_POLY : r < 7 ? M_DUAL : r < 9 ? M_UNISON : M_MONO);
    d->v[P_UNI_N] = (uint8_t)rn(2, 6);
    d->v[P_UNI_DET] = (uint8_t)rn(10, 80);
    d->v[P_GLIDE] = (uint8_t)(d->v[P_MODE] >= M_UNISON && rn(0, 1) ? rn(5, 40) : 0);
    d->v[P_VINTAGE] = (uint8_t)rn(0, 100);
    d->v[P_SPREAD] = (uint8_t)rn(20, 127);
    d->v[P_CHO_MIX] = (uint8_t)(rn(0, 2) ? 0 : rn(30, 100));
    d->v[P_DLY_TIME] = (uint8_t)rn(0, NDDIV - 1);
    d->v[P_DLY_FB] = (uint8_t)rn(20, 80);
    d->v[P_DLY_MIX] = (uint8_t)(rn(0, 1) ? 0 : rn(20, 70));
    d->v[P_REV_SIZE] = (uint8_t)rn(40, 127);
    d->v[P_REV_MIX] = (uint8_t)(rn(0, 3) ? rn(15, 60) : 0);
    trk_edited[seq_trk % NTRK] = 1;
    if (d == &live_p[0]) fx_keep(d);            /* (the dice leave the effects alone)*/
}

/* ---- the screen (obv_ui.h draws; this decides what and when) */
#include "obv_ui.h"

static void fmt_u(char *s, uint32_t v)          /* decimal, no leading zeros */
{
    char b[12];
    uint32_t i = 0, j = 0;
    do { b[i++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (i) s[j++] = b[--i];
    s[j] = 0;
}
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void scat(char *d, const char *s) { d += slen(d); while ((*d++ = *s++)) ; }

static void fmt_ms(char *s, uint32_t ms_x10)
{
    char t[4];
    if (ms_x10 < 1000u) { fmt_u(s, ms_x10 / 10u); scat(s, "."); fmt_u(t, ms_x10 % 10u); scat(s, t); scat(s, "ms"); }
    else if (ms_x10 < 10000u) { fmt_u(s, ms_x10 / 10u); scat(s, "ms"); }
    else { fmt_u(s, ms_x10 / 10000u); scat(s, "."); fmt_u(t, (ms_x10 / 1000u) % 10u); scat(s, t); scat(s, "s"); }
}

static void param_value(char *s, uint32_t p, uint32_t v)   /* <= 7 characters */
{
    char t[8];
    s[0] = 0;
    switch (PDEF[p].kind) {
    case K_BIP: if (v < 64u) { scat(s, "-"); fmt_u(t, 64u - v); } else { scat(s, v > 64u ? "+" : ""); fmt_u(t, v - 64u); } scat(s, t); break;
    case K_ONOFF: scat(s, v ? "ON" : "OFF"); break;
    case K_WAVE: scat(s, WAVE_NAME[v & 3u]); break;
    case K_FTYPE: scat(s, FTYPE_NAME[v % NFTYPE]); break;
    case K_MODE: scat(s, MODE_NAME[v & 3u]); break;
    case K_LSHAPE: scat(s, LSHAPE_NAME[v % 6u]); break;
    case K_LPHASE: scat(s, LPHASE_NAME[v % 3u]); break;
    case K_TO: scat(s, TO_NAME[v % 3u]); break;
    case K_ARP: scat(s, ARP_NAME[v % NARPMODE]); break;
    case K_DIV: scat(s, DIV_NAME[v % NDIV]); break;
    case K_DDIV: scat(s, DDIV_NAME[v % NDDIV]); break;
    case K_MORPH:                               /* LP64 .. NOTCH .. HP63 */
        if (v == 64u) scat(s, "NOTCH");
        else { scat(s, v < 64u ? "LP " : "HP "); fmt_u(t, v < 64u ? 64u - v : v - 64u); scat(s, t); }
        break;
    case K_SEMI: if (v < 24u) { scat(s, "-"); fmt_u(t, 24u - v); } else { scat(s, v > 24u ? "+" : ""); fmt_u(t, v - 24u); } scat(s, t); break;
    case K_MS:
        if (p == P_GLIDE) { if (!v) scat(s, "OFF"); else { fmt_u(s, GLIDE_MS[v & 127u]); scat(s, "ms"); } }
        else fmt_ms(s, TIME_MS_X10[v & 127u]);
        break;
    case K_HZ: { uint32_t h = LFO_HZ_X100[v & 127u]; fmt_u(s, h / 100u); scat(s, "."); fmt_u(t, (h / 10u) % 10u); scat(s, t); scat(s, "Hz"); } break;
    case K_PCT: fmt_u(s, (v * 100u + 63u) / 127u); scat(s, "%"); break;
    case K_CUT: { uint32_t hz = CUT_HZ[(v + 8u) % 137u]; if (hz < 10000u) { fmt_u(s, hz); scat(s, "Hz"); } else { fmt_u(s, hz / 1000u); scat(s, "."); fmt_u(t, (hz / 100u) % 10u); scat(s, t); scat(s, "k"); } } break;
    case K_OCT: fmt_u(s, v); scat(s, " OCT"); break;
    default: fmt_u(s, v);
    }
}

static uint32_t cut_shown_t;                    /* the header shows the cutoff for a second after it moved */
static void draw_header(void)
{
    char tags[40];
    tags[0] = 0;
    if (save_armed) {                           /* SAVE armed: the slot it saves into and what is there now */
        patch_t t;
        prog_get(save_to, &t);
        scat(tags, "SAVE HERE? " NM_PRESETS);
        ui_header(save_to, t.v + NPARAM, 0, tags, seq_trk);
        return;
    }
    if (cut_shown_t && fm1_ms + 1u - cut_shown_t < 1000u) {
        char v[12];
        param_value(v, P_CUTOFF, ed_patch(P_CUTOFF)->v[P_CUTOFF]);
        scat(tags, "CUTOFF ");
        scat(tags, v);
        ui_header(ed_prog(), ed_patch(0)->v + NPARAM, ed_edited(), tags, seq_trk);
        return;
    }
    {   /* short words; mode, filter, HOLD, SEQ and REC always (at most 23 characters: MONO NOTCH HOLD SEQ REC); then,
         * as far as they fit, ARP, FX */
        static const char *const MODE_S[4] = {"POLY", "DUAL", "UNI", "MONO"};
        static const char *const FT_S[NFTYPE] = {"LP12", "HP12", "BP12", "NOTCH", "LP24", "SEM"};
        const uint8_t *pv = ed_patch(0)->v;     /* (the mode and filter of the sound the panel is on) */
        uint32_t arp = pv[P_ARP_MODE] != 0, fx = live.v[P_CHO_MIX] || live.v[P_DLY_MIX] || live.v[P_REV_MIX];
        uint32_t n = slen(MODE_S[pv[P_MODE] & 3u]) + 1u + slen(FT_S[pv[P_FTYPE] % NFTYPE])
                   + ((hold >> seq_trk & 1u) ? 5u : 0u) + (ctl_seq_run ? 4u : 0u) + (rec ? 4u : 0u);
        if (arp && n + 4u <= 23u) n += 4u; else arp = 0;
        if (fx && n + 3u <= 23u) n += 3u; else fx = 0;
        scat(tags, MODE_S[pv[P_MODE] & 3u]);
        scat(tags, " ");
        scat(tags, FT_S[pv[P_FTYPE] % NFTYPE]);
        if (arp) scat(tags, " ARP");
        if (fx) scat(tags, " FX");
        if (hold >> seq_trk & 1u) scat(tags, " HOLD");
        if (ctl_seq_run) scat(tags, " SEQ");
        if (rec) scat(tags, " REC");
    }
    ui_header(ed_prog(), ed_patch(0)->v + NPARAM, ed_edited(), tags, seq_trk);
}

static void fmt_s(char *s, int32_t v)          /* signed: +3, -12, 0 */
{
    s[0] = 0;
    if (v) scat(s, v < 0 ? "-" : "+");
    fmt_u(s + slen(s), (uint32_t)(v < 0 ? -v : v));
}

static void fmt_note(char *s, uint32_t n)       /* C4, F#3 */
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    s[0] = 0;
    scat(s, NN[n % 12u]);
    if (n < 12u) scat(s, "-");
    fmt_u(s + slen(s), n < 12u ? 1u : n / 12u - 1u);
}

static void seq_knob(knob_view_t *k, uint32_t i)   /* the SEQ layer's knobs: the track's, or the held step's */
{
    static const char *const LT[4] = {"NOTE", "DIV", "SWING", "LENGTH"}, *const LS[4] = {"NOTE", "LEVEL", "RATCHET", "GATE"};
    seqtrk_t *tk = &pat.trk[seq_trk];
    char *v = k->value;
    k->present = 1;
    if (step_held >= 0) {
        step_t *st = seq_step(seq_trk, (uint32_t)step_held);
        uint32_t n;
        k->label = LS[i];
        if (i == 0) {
            if (!st->note[0]) scat(v, "EMPTY");
            else {
                for (n = 0; n < SEQ_CHORD && st->note[n]; n++) ;
                fmt_note(v, st->note[0]);
                if (n > 1u) { scat(v, "+"); fmt_u(v + slen(v), n - 1u); }
            }
            k->v = st->note[0]; k->max = 127;
        } else if (i == 1) { scat(v, LEVEL_NAME[level_of(st->vel)]); k->v = (int32_t)level_of(st->vel); k->max = 3; }
        else if (i == 2) { fmt_u(v, (st->ratch & 3u) + 1u); scat(v, "x"); k->v = st->ratch & 3u; k->max = 3; }
        else { if (st->gate >= GATE_TIE) scat(v, "TIE"); else { fmt_u(v, (st->gate * 100u + 63u) / 127u); scat(v, "%"); } k->v = st->gate; k->max = 127; }
    } else {
        k->label = LT[i];
        if (i == 0) { fmt_note(v, pen); if (pen_n > 1u) { scat(v, "+"); fmt_u(v + slen(v), pen_n - 1u); } k->v = (int32_t)pen; k->max = 127; }
        else if (i == 1) { scat(v, DIV_NAME[tk->div % NDIV]); k->v = tk->div; k->max = NDIV - 1; }
        else if (i == 2) { fmt_u(v, tk->swing); scat(v, "%"); k->v = tk->swing - 50; k->max = 25; }
        else { if (tk->len) fmt_u(v, tk->len); else scat(v, "OFF"); k->v = tk->len; k->max = SEQ_STEPS; }
    }
}

/* the other layers' knobs: EDIT reshapes the pattern, FX the effects' mixes; GLO / SAVE have none */
static void layer_knob(knob_view_t *k, uint32_t i)
{
    static const char *const LE[4] = {"SHIFT", "LENGTH", "TRANSP", ""};
    static const int8_t FXP[4] = {P_CHO_MIX, P_DLY_MIX, P_REV_MIX, P_DLY_TIME};
    k->present = 0;
    if (layer == L_EDIT && i < 3u) {
        k->present = 1; k->label = LE[i]; k->v = 0; k->max = 1;
        if (i == 1) { fmt_u(k->value, pat.trk[seq_trk].len); scat(k->value, " x2 1/2"); }
        else scat(k->value, i ? "+-" : "< >");
    } else if (layer == L_FX) {
        uint32_t p2 = (uint32_t)FXP[i];
        k->present = 1; k->label = PDEF[p2].name; k->v = live.v[p2]; k->max = PDEF[p2].max;   /* (the effects: A's) */
        param_value(k->value, p2, live.v[p2]);
    }
}

static void draw_page(void)
{
    knob_view_t k[4];
    uint32_t i;
    const page_t *pg = &PAGE[page];
    for (i = 0; i < 4u; i++) {
        int32_t p = pg->p[i];
        k[i].present = 0;
        k[i].bip = 0;
        k[i].value[0] = 0;
        if (page == PG_MIXER && !layer) {      /* the mixer: A-D, the row LEVEL / PAN / SEND (F1 again: the next row) */
            static const char *const ML[NTRK] = {"A", "B", "C", "D"}, *const MM[4] = {"VOLUME", "FILTER", "DRIVE", "COMP"};
            uint32_t t = i;
            k[i].label = mix_row == 6u ? MM[t] : ML[t];
            k[i].present = 1;
            if (mix_row == 6u) {                /* (VIBE) MASTER */
                if (t == 0u) { fmt_u(k[i].value, set.m_vol); k[i].v = set.m_vol; k[i].max = 127; }
                else if (t == 1u) { int32_t d = (int32_t)set.m_dj - 64;
                    if (d > -2 && d < 2) scat(k[i].value, "OFF"); else { scat(k[i].value, d < 0 ? "LP " : "HP "); fmt_u(k[i].value + 3, (uint32_t)(d < 0 ? -d : d)); }
                    k[i].v = set.m_dj; k[i].max = 128; k[i].bip = 1; }
                else { uint32_t v = t == 2u ? set.m_drive : set.m_comp; fmt_u(k[i].value, (v * 100u + 63u) / 127u); scat(k[i].value, "%"); k[i].v = v; k[i].max = 127; }
            }
            else if (mix_row == 0u) { fmt_u(k[i].value, set.mix_lvl[t]); k[i].v = set.mix_lvl[t]; k[i].max = 127; }
            else if (mix_row == 1u) {
                int32_t pn = set.mix_pan[t];
                if (!pn) scat(k[i].value, "C"); else { scat(k[i].value, pn < 0 ? "L" : "R"); fmt_u(k[i].value + 1, (uint32_t)(pn < 0 ? -pn : pn)); }
                k[i].v = pn + 64; k[i].max = 128; k[i].bip = 1;
            } else { const uint8_t *sv = mix_row == 2u ? set.mix_cho : mix_row == 3u ? set.mix_dly : mix_row == 5u ? set.mix_drv : set.mix_rev;
                     fmt_u(k[i].value, ((uint32_t)sv[t] * 100u + 63u) / 127u); scat(k[i].value, "%"); k[i].v = sv[t]; k[i].max = 127; }
        } else if (page == PG_DRIVE && !layer) {   /* (VIBE) the drive send's effect */
            static const char *const DL[4] = {"TYPE", "DRIVE", "TONE", "MIX"};
            k[i].label = DL[i];
            k[i].present = 1;
            if (i == 0) { scat(k[i].value, DV_NAME[set.dv_type % NDV]); k[i].v = set.dv_type % NDV; k[i].max = NDV - 1; }
            else { uint32_t v = i == 1 ? set.dv_amt : i == 2 ? set.dv_tone : set.dv_mix; fmt_u(k[i].value, v); k[i].v = v; k[i].max = 127; }
        } else if (page == PG_GLOBAL && !layer && !SMK_PAD_HELD) {
            static const char *const GL[4] = {"TEMPO", "CLICK", "MIDI CH", "TUNE"};
            k[i].label = GL[i];
            k[i].present = 1;
            if (i == 0) { fmt_u(k[i].value, set.tempo); k[i].v = set.tempo - 40; k[i].max = 200; }
            else if (i == 1) { scat(k[i].value, CLICK_NAME[set.click % NCLICK]); k[i].v = set.click % NCLICK; k[i].max = NCLICK - 1; }
            else if (i == 2) { if (set.midi_ch) fmt_u(k[i].value, set.midi_ch); else scat(k[i].value, "OMNI"); k[i].v = set.midi_ch; k[i].max = 16; }
            else { scat(k[i].value, set.tune < 0 ? "-" : "+"); fmt_u(k[i].value + 1, (uint32_t)(set.tune < 0 ? -set.tune : set.tune)); scat(k[i].value, "c"); k[i].v = set.tune + 50; k[i].max = 100; k[i].bip = 1; }
        } else if (layer == L_EDIT || layer == L_FX || layer == L_GLO || layer == L_SAVE) {
            layer_knob(&k[i], i);
        } else if (seq_shown()) {
            seq_knob(&k[i], i);
        } else if (p >= 0) {
            k[i].label = PDEF[p].name;
            k[i].v = ed_patch((uint32_t)p)->v[p];
            k[i].max = PDEF[p].max;
            k[i].bip = PDEF[p].kind == K_BIP || PDEF[p].kind == K_SEMI || PDEF[p].kind == K_MORPH;
            k[i].present = 1;
            param_value(k[i].value, (uint32_t)p, (uint32_t)ed_patch((uint32_t)p)->v[p]);
        }
    }
    {
        static char title[24];
        const char *t = pg->title;
        if (layer == L_EDIT) { title[0] = 0; scat(title, "EDIT "); title[5] = TRK_L[seq_trk % NTRK]; title[6] = 0; scat(title, ": ERASE"); t = title; }
        else if (page == PG_MIXER && !layer) t = mix_row == 0u ? "MIXER  LEVEL" : mix_row == 1u ? "MIXER  PAN" : mix_row == 2u ? "SEND  CHORUS" : mix_row == 3u ? "SEND  DELAY" : mix_row == 4u ? "SEND  REVERB" : mix_row == 5u ? "SEND  DRIVE" : "MASTER";
        else if (layer == L_GLO) t = "GLO: MUTE SOLO TAP";
        else if (layer == L_SAVE) t = "SAVE: PATTERNS";
        else if (layer == L_FX) t = "FX MIX";
        else if (seq_shown()) {                 /* SEQ A, or STEP 5 A while a step is held */
            title[0] = 0;
            if (step_held >= 0) { scat(title, "STEP "); fmt_u(title + slen(title), (uint32_t)step_held + 1u); scat(title, " "); }
            else scat(title, "SEQ ");
            { char l[2] = {TRK_L[seq_trk % NTRK], 0}; scat(title, l); }
            if (trk_silent(seq_trk)) scat(title, " MUTED");
            t = title;
        }
        {
            static char cpu[24];                /* the GLOBAL page: the render time of an audio half (budget 5800 us), SYNC */
            cpu[0] = 0; fmt_u(cpu, ui_render_us); scat(cpu, "us"); scat(cpu, set.sync_int ? " INT" : ctl_ext_quarter ? " CLK" : " AUTO");
            ui_page(t, k, save_armed ? "SAVE?" : seq_shown() && pat_edited ? "EDITED" : page == PG_GLOBAL && !layer ? cpu : 0);
        }
    }
}

static void draw_steps(void)
{
    static char l[32];
    uint32_t t = seq_trk, len = pat.trk[t].len, ph = len ? ui_seq_pos[t] % len : 0u;
    int32_t sel = step_held;
    l[0] = 0;
    l[0] = TRK_L[t % NTRK]; l[1] = 0; scat(l, "  PAGE ");
    fmt_u(l + slen(l), seq_pg[t] + 1u); scat(l, "/"); fmt_u(l + slen(l), seq_pages(t));
    scat(l, "  PAT ");
    fmt_u(l + slen(l), set.pat + 1u);
    ui_steps(t, seq_pg[t] * 16u, sel, ctl_seq_run && len ? (int32_t)ph : -1, rec, l,
             rec == REC_ON ? "REC" : rec == REC_READY ? "REC READY" : ctl_seq_run ? "PLAY" : "STOP");
}

/* the hint lines under the strip: what the keys do right now (a toast after an action, else a slow rotation) */
static uint32_t hints_key = 0xFFFFFFFFu;
static void draw_hints(void)
{
    static const char *const ROT[3][2] = {
#if BOARD_SMK37
        {"PADS ARE STEPS  < >: PAGE", "HOLD " NM_EDIT ": KEYS ERASE"},
#else
        {"HOLD SEQ: KEYS ARE STEPS", "HOLD " NM_EDIT ": KEYS ERASE"},
#endif
        {NM_ALGO ": TRACK A-D", "HOLD GLO: START/STOP MUTE"},
        {"PLAY: START  REC: RECORD LIVE", "REC HELD=CLEAR " NM_EDIT "+KEY=ERASE"}};
    static char ring[24];
    uint32_t r = (fm1_ms / 3500u) % 3u, key, held = rec_down ? fm1_ms - rec_down_t : 0u;
    int toasting = toast && fm1_ms - toast_t < 1500u;
    if (held > 500u) {                          /* REC held: a bar fills up to the clear */
        uint32_t k, f = (held - 500u) * 12u / 700u;
        ring[0] = 0;
        scat(ring, "[");
        for (k = 0; k < 12u; k++) scat(ring, k < f ? "#" : "-");
        scat(ring, "]");
        { static char m[32]; m[0] = 0; scat(m, "KEEP HOLDING: CLEAR TRACK "); m[slen(m) + 1] = 0; m[slen(m)] = TRK_L[seq_trk % NTRK]; ui_hints(m, ring, C_RED); }
        hints_key = 0xFFFFFFFEu;
        return;
    }
    key = toasting ? 100u + ((toast_t / 7u) & 0xFFu) : rec == REC_ON ? 210u + ui_seq_count : rec == REC_READY ? 201u + ctl_seq_run : step_held >= 0 ? 300u : layer ? 400u + layer : r;
    if (key == hints_key) return;
    hints_key = key;
    if (toasting) ui_hints(toast, "", C_AMBER);
    else if (rec == REC_ON && ui_seq_count) { static char cnt[16]; cnt[0] = 0; scat(cnt, "COUNT-IN  "); fmt_u(cnt + slen(cnt), 5u - ui_seq_count); ui_hints(cnt, "RECORDING FROM THE NEXT 1", C_RED); }
    else if (rec == REC_ON) { static char m[24]; m[0] = 0; scat(m, "RECORDING TRACK "); m[slen(m) + 1] = 0; m[slen(m)] = TRK_L[seq_trk % NTRK]; ui_hints(m, "PLAY ON TOP  REC: STOP REC", C_RED); }
    else if (rec == REC_READY && ctl_seq_run) ui_hints("REC ARMED", "GLO + KEY 1-4: RECORD A TRACK", C_RED);
    else if (rec == REC_READY) ui_hints("REC READY", "YOUR FIRST NOTE STARTS IT", C_RED);
    else if (step_held >= 0) ui_hints("KNOB: EDIT  TAP LATER: LONG", "NOTE  LEVEL  RATCHET  GATE", C_AMBER);
    else if (layer == L_SEQ) ui_hints("WHITE KEYS = STEPS  OCT: PAGE", "F#3-A#4: PAGE 1-8 " NM_HOME ": LOCK", C_AMBER);
    else if (layer == L_EDIT) ui_hints("KEY: ERASE THAT NOTE", "OCT-: UNDO  OCT+: REDO", C_AMBER);
    else if (layer == L_GLO) ui_hints(rec ? "1-4: RECORD A-D  5-8: MUTE" : "1-4: START/STOP A-D 5-8: MUTE", "9-12: SOLO  G5: TAP TEMPO", C_AMBER);
    else if (layer == L_SAVE) ui_hints("WHITE 1-8: GO  BLACK 1-8: COPY", "+ " NM_SELBTN " PRESSED: NEW SOUND", C_AMBER);
    else if (layer == L_FX) ui_hints("KNOBS: CHORUS DELAY REVERB", "AND THE DELAY'S TIME", C_AMBER);
    else ui_hints(ROT[r][0], ROT[r][1], C_CREAM);
}

static void draw_tags(void) { draw_header(); }
static void draw_scope(void) { ui_scope(scope, SCOPE_N); }
static void draw_leds(void)                     /* (32 voices: eight lights, each the loudest of its card in all four groups) */
{
    static int32_t l8[NCARD];
    uint32_t v;
    for (v = 0; v < NCARD; v++) l8[v] = 0;
    for (v = 0; v < NV; v++) if (ui_vlevel[v] > l8[v % NCARD]) l8[v % NCARD] = ui_vlevel[v];
    ui_leds(l8, NCARD);
}

/* a knob in the SEQ layer turned by e: the held step's, else the track's (sloopDX's order) */
static void seq_turn(uint32_t knob, int32_t e)
{
    uint32_t t = seq_trk, k;
    seqtrk_t *tk = &pat.trk[t];
    int32_t sg = e > 0 ? 1 : -1;
    if (step_held >= 0) {
        step_t *st = seq_step(t, (uint32_t)step_held);
        step_turned = 1;
        if (knob == 0) {                        /* NOTE: the chord moves; an empty step gets the pen */
            if (!st->note[0]) { step_clear(st); st->note[0] = (uint8_t)pen; }
            else for (k = 0; k < SEQ_CHORD && st->note[k]; k++) st->note[k] = (uint8_t)clampi(st->note[k] + sg, 1, 127);
            st->ratch &= (uint8_t)~STEP_OFF;
        } else if (knob == 1) st->vel = LEVEL_VEL[clampi((int32_t)level_of(st->vel) + sg, 0, 3)];
        else if (knob == 2) st->ratch = (uint8_t)((st->ratch & STEP_OFF) | (uint32_t)clampi((int32_t)(st->ratch & 3u) + sg, 0, 3));
        else st->gate = (uint8_t)clampi((int32_t)st->gate + e * 2, 1, GATE_TIE);
    } else if (knob == 0) { pen = (uint32_t)clampi((int32_t)pen + e, 1, 127); pen_chord[0] = (uint8_t)pen; pen_n = 1; return; }
    else if (knob == 1) tk->div = (uint8_t)clampi((int32_t)tk->div + sg, 0, NDIV - 1);
    else if (knob == 2) tk->swing = (uint8_t)clampi((int32_t)tk->swing + e, 50, 75);
    else {                                      /* LENGTH: 0 = off .. 256 */
        tk->len = (uint16_t)clampi((int32_t)tk->len + e, 0, (int32_t)SEQ_STEPS);
        if (tk->len && seq_cur[t] >= tk->len) seq_cur[t] = tk->len - 1u;
        if (seq_pg[t] >= seq_pages(t)) seq_pg[t] = seq_pages(t) - 1u;
    }
    pat_edited = 1;
}

/* EDIT held, a knob: SHIFT every step one later / earlier, LENGTH x2 (the pattern after itself) / half, TRANSPOSE */
static void edit_turn(uint32_t knob, int32_t e)
{
    uint32_t t = seq_trk, len = pat.trk[t].len, k, n;
    int32_t sg = e > 0 ? 1 : -1;
    if (!len || knob > 2u) return;
    if (!layer_used) undo_save();               /* (one hold, one undo) */
    layer_used = 1;
    if (knob == 0) {
        step_t keep = sg > 0 ? *seq_step(t, len - 1u) : *seq_step(t, 0);
        if (sg > 0) { for (n = len - 1u; n; n--) *seq_step(t, n) = *seq_step(t, n - 1u); *seq_step(t, 0) = keep; }
        else { for (n = 0; n + 1u < len; n++) *seq_step(t, n) = *seq_step(t, n + 1u); *seq_step(t, len - 1u) = keep; }
        say(sg > 0 ? "SHIFTED LATER" : "SHIFTED EARLIER");
    } else if (knob == 1) {
        if (sg > 0) {
            if (2u * len > SEQ_STEPS) { say("STEPS FULL"); return; }
            for (n = 0; n < len; n++) *seq_step(t, len + n) = *seq_step(t, n);
            pat.trk[t].len = (uint16_t)(2u * len);
            say("LENGTH X2");
        } else if (len > 1u) { pat.trk[t].len = (uint16_t)(len / 2u); say("LENGTH 1/2"); }
        if (seq_pg[t] >= seq_pages(t)) seq_pg[t] = seq_pages(t) - 1u;
    } else {
        for (n = 0; n < len; n++) {
            step_t *st = seq_step(t, n);
            for (k = 0; k < SEQ_CHORD && st->note[k]; k++) st->note[k] = (uint8_t)clampi(st->note[k] + sg, 1, 127);
        }
        say(sg > 0 ? "TRANSPOSED UP" : "TRANSPOSED DOWN");
    }
    pat_edited = 1;
}

static void audition_off(void)
{
    uint32_t k;
    for (k = 0; k < SEQ_CHORD; k++)
        if (audition[k]) { note_event_p(audition_part, 0, audition[k], 0); audition[k] = 0; }
}

/* a key went down in the step view: a white key picks its step (a tap switches it on / off when it comes up),
 * a black key is a function */
static void seq_key_down(uint32_t k)
{
    static char msg[20];
    uint32_t t = seq_trk, len = pat.trk[t].len, n, i;
    seqtrk_t *tk = &pat.trk[t];
    layer_used = 1;
    if (KEY_STEP[k] >= 0) {
        n = seq_pg[t] * 16u + (uint32_t)KEY_STEP[k];
        if (n >= len) { say(len ? "PAST THE TRACK'S LENGTH" : "TRACK OFF: TURN LENGTH"); return; }
        if (step_held >= 0 && n > (uint32_t)step_held) {   /* hold one step, tap a later one: a long note to there */
            step_t *a = seq_step(t, (uint32_t)step_held);
            uint32_t m;
            static char msg[24];
            undo_save();
            if (!a->note[0]) { step_clear(a); a->note[0] = (uint8_t)pen; }
            a->ratch &= (uint8_t)~STEP_OFF;
            for (m = (uint32_t)step_held; m <= n; m++) {
                step_t *st = seq_step(t, m);
                if (m != (uint32_t)step_held) { uint8_t r = (uint8_t)(a->ratch & 3u); *st = *a; st->ratch = r; }
                st->gate = m < n ? GATE_TIE : 110;
            }
            step_turned = 1;                    /* (the held step is kept when it is let go) */
            pat_edited = 1;
            msg[0] = 0; scat(msg, "LONG NOTE "); fmt_u(msg + slen(msg), (uint32_t)step_held + 1u); scat(msg, "-"); fmt_u(msg + slen(msg), n + 1u);
            say(msg);
            return;
        }
        step_held = (int32_t)n;
        step_held_t = fm1_ms;
        step_turned = step_midi = 0;
        seq_cur[t] = n;
        {
            step_t *st = seq_step(t, n);
            if (!st->note[0] || st->ratch & STEP_OFF) {   /* an empty step is set at once, with the last note or chord */
                uint32_t q;
                undo_save();
                step_clear(st);
                for (q = 0; q < (pen_n ? pen_n : 1u) && q < SEQ_CHORD; q++) st->note[q] = pen_n ? pen_chord[q] : (uint8_t)pen;
                STEP_NEW_VEL(st);               /* (SMK-37: the velocity of the key or pad that set it) */
                step_turned = 1;                /* (kept when it is let go) */
                pat_edited = 1;
            }
        }
        if (!ctl_seq_run) {                     /* stopped: you hear the step you hold */
            const step_t *st = seq_step(t, n);
            audition_off();
            audition_part = (uint8_t)(t % NTRK); /* (in the track's sound) */
            for (i = 0; i < SEQ_CHORD && st->note[i]; i++) {
                audition[i] = (uint8_t)clampi(st->note[i] + tk->transp, 1, 127);
                note_event_p(audition_part, 1, audition[i], st->vel ? st->vel : 100u);
            }
        }
        return;
    }
    if (KEY_PAGE[k] >= 0) {                     /* a black key: that page */
        if ((uint32_t)KEY_PAGE[k] < seq_pages(t)) {
            seq_pg[t] = (uint32_t)KEY_PAGE[k];
            msg[0] = 0; scat(msg, "PAGE "); fmt_u(msg + slen(msg), seq_pg[t] + 1u); scat(msg, " OF "); fmt_u(msg + slen(msg), seq_pages(t));
            say(msg);
        } else say("NO SUCH PAGE: TURN LENGTH");
    }
    (void)tk; (void)i;
}

static void seq_key_up(uint32_t k)
{
    uint32_t t = seq_trk;
    if (KEY_STEP[k] >= 0) {
        uint32_t n = seq_pg[t] * 16u + (uint32_t)KEY_STEP[k];
        if (step_held == (int32_t)n) {
            if (!step_turned && !step_midi) {   /* a set step pressed and let go (not edited): cleared */
                undo_save();
                step_clear(seq_step(t, n));
                pat_edited = 1;
            }
            step_held = -1;
            audition_off();
        }
    }
}

/* the last note or chord played (keys or MIDI): notes struck together make the chord an empty step gets */
static void pen_add(uint32_t note)
{
    if (!notes_down) pen_n = 0;
    notes_down++;
    if (pen_n < 4u) pen_chord[pen_n++] = (uint8_t)note;
    pen = note;
}

/* the mixer's mute and solo (GLO held: white keys 1-4 / 5-8; VIBE: the pads' third and second rows) */
static void mix_mute(uint32_t t)
{
    static char msg[12];
    t %= NTRK;
    set.mix_mute ^= (uint8_t)(1u << t);
    mix_apply(); set_dirty = 1; set_t = fm1_ms;
    msg[0] = TRK_L[t]; msg[1] = 0; scat(msg, set.mix_mute >> t & 1u ? " MUTED" : " ON");
    say(msg);
}
static void mix_solo(uint32_t t)
{
    static char msg[12];
    t %= NTRK;
    set.mix_solo ^= (uint8_t)(1u << t);
    mix_apply(); set_dirty = 1; set_t = fm1_ms;
    msg[0] = 0; scat(msg, set.mix_solo >> t & 1u ? "SOLO " : "SOLO OFF "); msg[slen(msg) + 1] = 0; msg[slen(msg)] = TRK_L[t];
    say(msg);
}

/* GLO held, the tracks' layer (VIBE's pads): white keys 1-4 start / stop tracks A-D on the bar (REC lit: that track
 * records), 5-8 mute them, 9-12 solo them, the last (G5) taps the tempo */
static void trk_launch(uint32_t t);
static void trk_record(uint32_t t);
static void glo_key(uint32_t k)
{
    static char msg[16];
    int32_t s2 = KEY_STEP[k];
    if (s2 >= 0 && s2 < 4) { if (rec) trk_record((uint32_t)s2); else trk_launch((uint32_t)s2); }
    else if (s2 >= 4 && s2 < 8) mix_mute((uint32_t)(s2 - 4));
    else if (s2 >= 8 && s2 < 12) mix_solo((uint32_t)(s2 - 8));
    else if (s2 == 15) {                        /* tap tempo: the mean of the last taps */
        tap_t[2] = tap_t[1]; tap_t[1] = tap_t[0]; tap_t[0] = fm1_ms;
        if (tap_t[1] && tap_t[0] - tap_t[1] < 2000u) {
            uint32_t span = tap_t[2] && tap_t[1] - tap_t[2] < 2000u ? (tap_t[0] - tap_t[2]) / 2u : tap_t[0] - tap_t[1];
            if (span) { set.tempo = (uint8_t)clampi((int32_t)(60000u / span), 40, 240); ctl_tempo = set.tempo; set_dirty = 1; set_t = fm1_ms; }
            msg[0] = 0; scat(msg, "TEMPO "); fmt_u(msg + slen(msg), set.tempo);
            say(msg);
        } else say("TAP AGAIN");
    }
}

/* ---- the full-screen sequencer, as SLOOP's STEP page: KNOB 1 moves the cursor, the keys write their note or
 * chord into the cursor's step and the cursor goes on when they are let go (a key held + KNOB 1 on: a long note),
 * EDIT tapped clears the step. REC records live as on every page. */
static int entry_active(void)
{
    return is_seq_page(page) && SV_ENTRY(seq_view) && !rec && !layer && step_held < 0 && pat.trk[seq_trk].len;
}

static void entry_note(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t t = seq_trk, len = pat.trk[t].len, k;
    step_t *st;
    note &= 127u;
    if (!on) {
        if (!entry_on_note[note]) return;
        entry_on_note[note] = 0;
        if (entry_n && !--entry_n && len) seq_cursor_set(t, (seq_cur[t] + 1u) % len);   /* all let go: the next step */
        return;
    }
    if (!entry_active() || entry_on_note[note]) return;
    if (seq_cur[t] >= len) seq_cursor_set(t, 0);
    st = seq_step(t, seq_cur[t]);
    if (!entry_n) {                             /* the first key: the step's notes are replaced */
        undo_save();
        if (!st->note[0] || st->ratch & STEP_OFF) step_clear(st);
        for (k = 0; k < SEQ_CHORD; k++) st->note[k] = 0;
        if (st->gate >= GATE_TIE) st->gate = 96;
        st->ratch &= (uint8_t)~STEP_OFF;
        st->vel = (uint8_t)(vel ? vel : 100u);
    }
    step_add(st, (uint32_t)clampi((int32_t)note - pat.trk[t].transp, 1, 127), st->vel);
    entry_on_note[note] = 1;
    entry_n++;
    pat_edited = 1;
}

static void entry_reset(void) { entry_n = 0; memset(entry_on_note, 0, sizeof entry_on_note); }

/* which slots hold a pattern, its tracks' lengths, and every pattern's sounds (at boot; undo_pat as the buffer) */
static void slot_lens_read(void)
{
    uint32_t i, t;
    pat_used = 0;
    for (i = 0; i < NPAT; i++) {
        psnd[i].magic = 0;
        if (!slot_load(i, &undo_pat, sizeof undo_pat, &psnd[i], sizeof psnd[i]) && undo_pat.magic == PAT_MAGIC) {
            if (psnd[i].magic != PSN2_MAGIC) psnd[i].magic = 0;
        } else {
            psnd[i].magic = 0;
            if (pat_from_v1(i, &undo_pat)) continue;   /* (1.5's pattern: converted, its sounds in psnd[i]) */
        }
        pat_used |= 1u << i;
        for (t = 0; t < NTRK; t++) slot_len[i][t] = undo_pat.trk[t].len;
    }
}

static void slot_saved(uint32_t n, const pattern_t *p)
{
    uint32_t t;
    pat_used |= 1u << n;
    for (t = 0; t < NTRK; t++) slot_len[n][t] = p->trk[t].len;
    snd_dirty &= ~(1u << n);                    /* (its sounds went with it) */
}

/* another pattern, now (stopped), with its sounds and its mix. This one's edits are written first; a pattern left
 * while playing (pat_old, from the ISR's swap) waits there for the stop */
static void pat_switch(uint32_t n)
{
    static char msg[20];
    n %= NPAT;
    if (n != set.pat) {
        if (snd_changed(set.pat)) snd_capture(set.pat);   /* (its instruments stay with it) */
        if (pat_edited) { pat_dirty = 1; pat_next = n + 1u; return; }   /* saved when quiet, then loaded */
        load_pattern(n);
    }
    slot_sel = n;
    set_dirty = 1; set_t = fm1_ms;
    msg[0] = 0; scat(msg, "PATTERN "); fmt_u(msg + slen(msg), n + 1u);
    if (!(pat_used >> n & 1u) && !pat_edited) scat(msg, " (NEW)");
    say(msg);
}

/* stopped, at once; playing, on the next bar: the ISR swaps pat_q in on the downbeat, and its sounds (snd_q) and its
 * mix (mx_q) on the same one, so the new pattern's first note plays its own instrument */
static void pat_go(uint32_t n)
{
    static char msg[20];
    n %= NPAT;
    if (pat_queue) {                            /* another one was next: take it back (the ISR never stops us halfway) */
        fm1_irq_off();
        if (ctl_pat_swap == 1u) { ctl_pat_swap = 0; ctl_snd_swap = 0; ctl_mx_swap = 0; pat_queue = 0; }
        fm1_irq_on();
        if (pat_queue) { say("ONE MOMENT"); return; }
    }
    if (!ctl_seq_run) { pat_switch(n); return; }
    if (n == set.pat) { say("STAYS"); return; }
    if (pend_slot && pat_edited) { say("STOP ONCE: THE LAST ONE SAVES"); return; }   /* (pat_old holds one, unsaved) */
    if (snd_changed(set.pat)) snd_capture(set.pat);
    if (pend_slot == n + 1u) {                  /* back to the one left with its edits (not saved yet): those */
        pat_q = pat_old; pend_slot = 0; q_edited = 1;
    } else { pat_fetch(n, &pat_q); q_edited = 0; }
    ctl_pat_keep = !pend_slot;                  /* (the one left comes back in pat_old, with its edits; pat_old waiting
                                                 * for the stop is never overwritten) */
    snd_target(n, snd_q); ctl_snd_swap = 1;
    mx_prepare(n); ctl_mx_swap = 1;
    pat_queue = n + 1u;
    RING_PUBLISH();
    ctl_pat_swap = 1;
    msg[0] = 0; scat(msg, "PATTERN "); fmt_u(msg + slen(msg), n + 1u); scat(msg, " NEXT");
    say(msg);
}

/* this pattern into slot n, with its sounds (a used one: again); written when stopped and quiet */
static void pat_copy(uint32_t n)
{
    static char msg[24];
    n %= NPAT;
    msg[0] = 0;
    if (n == set.pat) { say("THAT IS THIS ONE"); return; }
    if (pat_used >> n & 1u && (copy_arm != n + 1u || fm1_ms - copy_arm_t > 3000u)) {
        copy_arm = n + 1u; copy_arm_t = fm1_ms;
        scat(msg, "AGAIN: OVERWRITE "); fmt_u(msg + slen(msg), n + 1u);
        say(msg);
        return;
    }
    copy_arm = 0;
    copy_to = n + 1u;
    say(ctl_seq_run ? "COPIES WHEN STOPPED" : "COPYING");
}

static void pat_save_to(uint32_t n)             /* the pattern now into slot n (written when stopped and quiet) */
{
    n %= NPAT;
    if (n != set.pat && pend_slot == n + 1u) pend_slot = 0;   /* (what waited for that slot is overwritten anyway) */
    set.pat = (uint8_t)n;
    set_dirty = 1; set_t = fm1_ms;
    pat_dirty = 1;
    pat_edited = 1;
    pat_save_say = 1;
    say(ctl_seq_run ? "SAVES WHEN STOPPED" : "SAVING");
}

static void seq_clear_cursor(void)              /* EDIT tapped: the cursor's step empty, the cursor on */
{
    uint32_t t = seq_trk, len = pat.trk[t].len;
    if (!len) return;
    undo_save();
    step_clear(seq_step(t, seq_cur[t] % len));
    seq_cursor_set(t, (seq_cur[t] + 1u) % len);
    pat_edited = 1;
    say("STEP CLEARED");
}

static void seq_view_turn(uint32_t knob, int32_t e)
{
    uint32_t t = seq_trk, len = pat.trk[t].len, k;
    seqtrk_t *tk = &pat.trk[t];
    int32_t sg = e > 0 ? 1 : -1;
    step_t *st;
    if (seq_view == SV_SLOTS) {                 /* SLOT; GO there; COPY this one there */
        if (knob == 0) slot_sel = (uint32_t)clampi((int32_t)slot_sel + sg, 0, NPAT - 1);
        else if (knob == 2u && e > 0) pat_go(slot_sel);
        else if (knob == 3u && e > 0) pat_copy(slot_sel);
        return;
    }
    if (seq_view == SV_TRACKS && (knob == 0 || knob == 2u)) knob ^= 2u;   /* (TRACKS: CUTOFF on knob 1, LENGTH on 3) */
    if (seq_view == SV_TRACKS && knob > 0) {    /* the track's mixer LEVEL, its sound's CUTOFF; REC's QUANTISE */
        patch_t *d = &live_p[t % NTRK];
        if (knob == 1) { set.mix_lvl[t] = (uint8_t)clampi((int32_t)set.mix_lvl[t] + e * 2, 0, 127); mix_apply(); set_dirty = 1; set_t = fm1_ms; }
        else if (knob == 2) set_param_in(d, P_CUTOFF, d->v[P_CUTOFF] + e * 2);
        else { set.rec_qt[t % NTRK] = (uint8_t)clampi((int32_t)(set.rec_qt[t % NTRK] % NRQ) + (e > 0 ? 1 : -1), 0, NRQ - 1); set_dirty = 1; set_t = fm1_ms; }
        return;
    }
    if (seq_view == SV_TRACK || seq_view == SV_TRACKS || (!len && knob == 0)) {   /* (TRACKS: KNOB 1 = LENGTH too) */   /* LENGTH (0 = off; a track off:
                                                         * KNOB 1 gives it a length on every view), DIV, SWING, TRANSPOSE */
        if (knob == 0) {
            int32_t l = (int32_t)tk->len;
            if (!l && e > 0) l = 16;            /* (switched on: a bar) */
            else if (e > 1 || e < -1) l = e > 0 ? (l / 16 + 1) * 16 : (l - 1) / 16 * 16;   /* fast: a page of 16 */
            else l += e;
            tk->len = (uint16_t)clampi(l, 0, (int32_t)SEQ_STEPS);
            if (tk->len && seq_cur[t] >= tk->len) seq_cursor_set(t, tk->len - 1u);
        } else if (knob == 1) tk->div = (uint8_t)clampi((int32_t)tk->div + sg, 0, NDIV - 1);
        else if (knob == 2) tk->swing = (uint8_t)clampi((int32_t)tk->swing + e, 50, 75);
        else tk->transp = (int8_t)clampi(tk->transp + sg, -24, 24);
        pat_edited = 1;
        return;
    }
    if (!len) return;
    if (knob == 0) {                            /* the cursor; keys held: the note goes on into the next step */
        if (entry_n && e > 0) {
            uint32_t n = seq_cur[t] % len;
            if (n + 1u >= len) return;
            st = seq_step(t, n);
            st->gate = GATE_TIE;
            *seq_step(t, n + 1u) = *st;
            seq_step(t, n + 1u)->gate = 110;
            seq_step(t, n + 1u)->ratch = 0;
            seq_cursor_set(t, n + 1u);
            pat_edited = 1;
            return;
        }
        seq_cursor_set(t, (uint32_t)(((int32_t)(seq_cur[t] % len) + e % (int32_t)len + (int32_t)len) % (int32_t)len));
        return;
    }
    st = seq_step(t, seq_cur[t] % len);
    if (SV_EFF == SV_STEPS && knob == 1) {      /* NOTE: the chord moves; an empty step gets the last notes played */
        if (!st->note[0]) {
            step_clear(st);
            for (k = 0; k < (pen_n ? pen_n : 1u) && k < SEQ_CHORD; k++) st->note[k] = pen_n ? pen_chord[k] : (uint8_t)pen;
        } else for (k = 0; k < SEQ_CHORD && st->note[k]; k++) st->note[k] = (uint8_t)clampi(st->note[k] + sg, 1, 127);
        st->ratch &= (uint8_t)~STEP_OFF;
    } else if (!st->note[0]) { say("EMPTY STEP: PLAY A NOTE"); return; }
    else if (SV_EFF == SV_STEPS && knob == 2) st->gate = (uint8_t)clampi((int32_t)st->gate + e * 2, 1, GATE_TIE);
    else if (SV_EFF == SV_STEPS) st->vel = (uint8_t)clampi((int32_t)st->vel + e * 2, 1, 127);
    else if (knob == 1) st->ratch = (uint8_t)((st->ratch & STEP_OFF) | (uint32_t)clampi((int32_t)(st->ratch & 3u) + sg, 0, 3));
    else if (knob == 2) st->chance = (uint8_t)clampi((int32_t)st->chance + sg * 5, 0, 100);
    else st->ratch = (uint8_t)(e > 0 ? st->ratch & ~STEP_OFF : st->ratch | STEP_OFF);
    pat_edited = 1;
}

static void seq_view_knob(knob_view_t *k, uint32_t i)
{
    static const char *const L[NSV][4] = {{"CUTOFF", "LEVEL", "LENGTH", "QUANT"}, {"STEP", "NOTE", "GATE", "VELO"},
                                          {"STEP", "RATCHET", "CHANCE", "ON/OFF"}, {"LENGTH", "DIV", "SWING", "TRANSP"},
                                          {"SLOT", "", "GO", "COPY"}};
    uint32_t t = seq_trk, len = pat.trk[t].len, n;
    seqtrk_t *tk = &pat.trk[t];
    const step_t *st = seq_step(t, len ? seq_cur[t] % len : 0u);
    char *v = k->value;
    k->label = L[SV_EFF][i];
    k->present = k->label[0] != 0;
    k->bip = 0; k->v = 0; k->max = 0;
    if (seq_view == SV_SLOTS) {
        if (i == 0) { fmt_u(v, slot_sel + 1u); k->v = (int32_t)slot_sel; k->max = NPAT - 1; }
        else if (i == 2u) scat(v, slot_sel == set.pat ? "HERE" : "TURN");
        else if (i == 3u) scat(v, copy_arm == slot_sel + 1u && fm1_ms - copy_arm_t <= 3000u ? "AGAIN" : "TURN");
        return;
    }
    if (seq_view == SV_TRACKS) {                /* (CUTOFF on knob 1, LENGTH on 3: as VIBE's 3.0) */
        const patch_t *d = &live_p[t % NTRK];
        i = i == 0u ? 2u : i == 2u ? 0u : i;
        if (i == 0) { if (tk->len) fmt_u(v, tk->len); else scat(v, "OFF"); k->v = tk->len; k->max = SEQ_STEPS; }
        else if (i == 1) { fmt_u(v, set.mix_lvl[t]); k->v = set.mix_lvl[t]; k->max = 127; }
        else if (i == 2) { param_value(v, P_CUTOFF, d->v[P_CUTOFF]); k->v = d->v[P_CUTOFF]; k->max = 127; }
        else { scat(v, RQ_NAME[set.rec_qt[t % NTRK] % NRQ]); k->v = set.rec_qt[t % NTRK] % NRQ; k->max = NRQ - 1; }
        return;
    }
    if (seq_view == SV_TRACK) {
        if (i == 0) { if (tk->len) fmt_u(v, tk->len); else scat(v, "OFF"); k->v = tk->len; k->max = SEQ_STEPS; }
        else if (i == 1) { scat(v, DIV_NAME[tk->div % NDIV]); k->v = tk->div; k->max = NDIV - 1; }
        else if (i == 2) { fmt_u(v, tk->swing); scat(v, "%"); k->v = tk->swing - 50; k->max = 25; }
        else { fmt_s(v, tk->transp); k->v = tk->transp + 24; k->max = 48; k->bip = 1; }
        return;
    }
    if (!len) { if (i == 0) { k->label = "LENGTH"; scat(v, "OFF"); } return; }
    if (i == 0) { fmt_u(v, seq_cur[t] % len + 1u); scat(v, "/"); fmt_u(v + slen(v), len); k->v = (int32_t)(seq_cur[t] % len); k->max = (int32_t)len - 1; return; }
    if (!st->note[0]) { scat(v, "--"); return; }
    if (SV_EFF == SV_STEPS) {
        if (i == 1) {
            for (n = 0; n < SEQ_CHORD && st->note[n]; n++) ;
            fmt_note(v, st->note[0]);
            if (n > 1u) { scat(v, "+"); fmt_u(v + slen(v), n - 1u); }
            k->v = st->note[0]; k->max = 127;
        } else if (i == 2) { if (st->gate >= GATE_TIE) scat(v, "TIE"); else { fmt_u(v, (st->gate * 100u + 63u) / 127u); scat(v, "%"); } k->v = st->gate; k->max = 127; }
        else { fmt_u(v, st->vel); k->v = st->vel; k->max = 127; }
    } else {
        if (i == 1) { fmt_u(v, (st->ratch & 3u) + 1u); scat(v, "x"); k->v = st->ratch & 3u; k->max = 3; }
        else if (i == 2) { fmt_u(v, st->chance); scat(v, "%"); k->v = st->chance; k->max = 100; }
        else { scat(v, st->ratch & STEP_OFF ? "OFF" : "ON"); k->v = !(st->ratch & STEP_OFF); k->max = 1; }
    }
}

/* the full screen: what to draw (the main loop calls these when something changed) */
static void draw_seq_top(void)
{
    static char mid[16], right[20];
    uint32_t t = seq_trk;
    mid[0] = right[0] = 0;
    scat(mid, "PAT "); fmt_u(mid + slen(mid), set.pat + 1u);
    if (pat_edited) scat(mid, "*");
    if (pat_queue) { scat(mid, ">"); fmt_u(mid + slen(mid), pat_queue); }
    if (trk_silent(t)) scat(mid, " MUTE");
    if (slen(SV_NAME[SV_EFF]) + slen(mid) < 13u) fmt_u(right, ctl_ext_quarter ? 60u * FS / ctl_ext_quarter : set.tempo);   /* (the tempo, where it fits) */
    scat(right, rec == REC_ON ? " REC" : rec == REC_READY ? " ARMED" : ctl_seq_run ? " PLAY" : " STOP");
    ui_trk = t;
    ui_seq_top(t, layer == L_SEQ ? "SEQ KEYS" : layer == L_EDIT ? "ERASE" : SV_NAME[SV_EFF], mid, right,
               rec ? C_RED : ctl_seq_run ? C_AMBER : C_DIMT, ctl_trk_on);
}

static int32_t seq_playhead(void)
{
    uint32_t len = pat.trk[seq_trk].len;
    return ctl_seq_run && len && !ctl_count ? (int32_t)(ui_seq_pos[seq_trk] % len) : -1;
}

static uint32_t seq_w0(void)                    /* the roll's first step: the cursor's 16 (recording: the playhead's) */
{
    int32_t ph = seq_playhead();
    if (rec == REC_ON && ph >= 0) return (uint32_t)ph / 16u * 16u;
    return seq_pg[seq_trk] * 16u;
}

/* (VIBE) TRACK: the whole track, not a step – so it looks different from STEPS: its length as bars of 16 (the steps
 * that play lit), and its note value, swing and transposition large, each in a box */
static void draw_seq_trackview(void)
{
    uint32_t t = seq_trk, len = pat.trk[t].len, i, bars = len > 64u ? (len + 63u) / 64u : 1u;
    const seqtrk_t *tk = &pat.trk[t];
    uint16_t c = C_TRKC[t % NTRK];
    char s[24];
    cv_begin(240, 112, C_PANEL);
    s[0] = 0; scat(s, "TRACK "); { char l[2] = {TRK_L[t % NTRK], 0}; scat(s, l); }
    cv_text(4, 2, &FONT_S, s, c);
    s[0] = 0; if (len) { fmt_u(s, len); scat(s, " STEPS"); } else scat(s, "OFF");
    cv_text(236 - text_w(&FONT_S, s), 2, &FONT_S, s, C_CREAM);
    for (i = 0; i < 64u; i++) {                 /* four bars of 16 (over 64 steps: each cell stands for several) */
        int32_t x = (int32_t)(i % 16u) * 15 + 1, y = 20 + (int32_t)(i / 16u) * 9;
        uint32_t n = i * bars;
        cv_rect(x, y, 13, 7, n < len ? c : C_KNOB);
    }
    {                                           /* the three values, large-ish in boxes */
        static const char *const L[3] = {"DIV", "SWING", "TRANSP"};
        for (i = 0; i < 3u; i++) {
            int32_t x = (int32_t)i * 80 + 2;
            cv_rect(x, 60, 76, 48, C_KNOB);
            cv_rect(x, 60, 76, 2, c);
            cv_text(x + (76 - text_w(&FONT_S, L[i])) / 2, 66, &FONT_S, L[i], C_DIMT);
            s[0] = 0;
            if (i == 0) scat(s, DIV_NAME[tk->div % NDIV]);
            else if (i == 1) { fmt_u(s, tk->swing); scat(s, "%"); }
            else fmt_s(s, tk->transp);
            cv_text(x + (76 - text_w(&FONT_S, s)) / 2, 86, &FONT_S, s, C_CREAM);
        }
    }
    cv_blit(0, 24);
}

static void draw_seq_roll(void)
{
    uint32_t t = seq_trk;
    ui_trk = t;
    ui_seq_roll(t, seq_w0(), rec == REC_ON || !pat.trk[t].len ? -1 : (int32_t)(seq_cur[t] % pat.trk[t].len), seq_playhead());
}

/* the line over the overview: a message, the recording, a held layer's keys, else what the cursor's step holds */
static void seq_info_line(char *s, uint16_t *c)
{
    uint32_t t = seq_trk, len = pat.trk[t].len, k;
    *c = C_TRKC[seq_trk % NTRK];                /* (messages in the track's colour) */
    s[0] = 0;
    if (toast && fm1_ms - toast_t < 1500u) { scat(s, toast); return; }
    *c = C_RED;
    if (rec_down && fm1_ms - rec_down_t > 500u) { scat(s, "KEEP HOLDING: CLEAR TRACK"); return; }
    if (rec == REC_ON && ui_seq_count) { scat(s, "COUNT-IN "); fmt_u(s + slen(s), 5u - ui_seq_count); return; }
    if (rec_wait) { scat(s, "TRACK "); s[6] = TRK_L[(rec_wait - 1u) % NTRK]; s[7] = 0; scat(s, " RECORDS ON THE BAR"); return; }
    if (rec == REC_ON) { scat(s, "RECORDING: PLAY ON TOP"); return; }
    if (rec == REC_READY) { scat(s, ctl_seq_run ? "REC ARMED: GLO+1-4 RECORD" : "REC READY: FIRST NOTE STARTS"); return; }
    *c = C_AMBER;
    if (layer == L_SEQ) { scat(s, step_held >= 0 ? "KNOBS EDIT THE HELD STEP" : "WHITE KEYS = 16 STEPS"); return; }
    if (layer == L_EDIT) { scat(s, "KEY: ERASE  OCT: UNDO/REDO"); return; }
    if (layer == L_SAVE) { scat(s, "WHITE 1-8: GO  BLACK: COPY"); return; }
    if (layer == L_GLO) { scat(s, "1-4 START  5-8 MUTE  9-12 SOLO"); return; }
    if (layer) { scat(s, "LET GO: BACK"); return; }
    *c = C_CREAM;
    if (seq_view == SV_TRACK) {
        uint32_t u;
        for (u = 0; u < NTRK; u++) {
            char l[3] = {TRK_L[u], ' ', 0};
            if (u) scat(s, " ");
            scat(s, l);
            if (pat.trk[u].len) fmt_u(s + slen(s), pat.trk[u].len); else scat(s, "-");
        }
        return;
    }
    if (!len) { *c = C_DIMT; { char l[2] = {TRK_L[t % NTRK], 0}; scat(s, l); } scat(s, " IS OFF: " NM_K1 " SWITCHES ON"); return; }
    {
        const step_t *st = seq_step(t, seq_cur[t] % len);
        fmt_u(s, seq_cur[t] % len + 1u);
        scat(s, ":");
        if (!st->note[0]) { scat(s, " EMPTY - PLAY A KEY"); *c = C_DIMT; return; }
        for (k = 0; k < SEQ_CHORD && st->note[k]; k++) {
            char nn[6];
            fmt_note(nn, (uint32_t)clampi(st->note[k] + pat.trk[t].transp, 1, 127));
            scat(s, " "); scat(s, nn);
        }
        if (st->ratch & STEP_OFF) scat(s, " (OFF)");
    }
}

static void draw_seq_info(void)
{
    static char line[40], right[12];
    uint32_t t = seq_trk, len = pat.trk[t].len, w0 = seq_w0();
    uint16_t c;
    seq_info_line(line, &c);
    ui_trk = t;
    right[0] = 0;
    if (ctl_seq_run && (ctl_trk_on >> t & 1u) && len > 16u && seq_playhead() >= 0 && (uint32_t)seq_playhead() / 16u != w0 / 16u) {
        scat(right, "PLAYS "); fmt_u(right + slen(right), (uint32_t)seq_playhead() / 16u + 1u);   /* (another bar shown than plays) */
        if (slen(line) > 13u) {                 /* (the line gives way: at its " - ", else after 13) */
            uint32_t q;
            for (q = 0; line[q] && !(line[q] == ' ' && line[q + 1u] == '-'); q++) ;
            line[q < 13u ? q : 13u] = 0;
        }
    } else if (len > 64u && slen(line) < 22u) { /* which 64 the cells show */
        uint32_t a = w0 / 64u * 64u;
        fmt_u(right, a + 1u); scat(right, "-"); fmt_u(right + slen(right), a + 64u < len ? a + 64u : len);
    }
    ui_seq_info(t, line, c, right[0] ? right : 0, w0, rec == REC_ON || !len ? -1 : (int32_t)(seq_cur[t] % len), seq_playhead());
}

/* TRACKS: A-D one under the other (as SLOOP's, smaller), each with its sound, its state (playing, stopped, waiting for
 * the bar), its 16 steps and the playhead */
static uint32_t trk_state(uint32_t t)           /* 0 stopped, 1 playing, 2 starts on the bar, 3 stops on the bar */
{
    uint32_t on = ctl_trk_on >> t & 1u, pend = trk_pending() >> t & 1u;
    return pend ? (on ? 3u : 2u) : on;
}
static void draw_seq_tracks(void)
{
    static char title[NTRK][28];
    static const char *const ST[4] = {"STOP", "PLAY", "NEXT", "ENDS"};
    uint32_t t, k;
    for (t = 0; t < NTRK; t++) {
        uint32_t len = pat.trk[t].len, st = trk_state(t);
        const patch_t *d = &live_p[t];
        int32_t ph = ctl_seq_run && len && !ctl_count && (ctl_trk_on >> t & 1u) ? (int32_t)(ui_seq_pos[t] % len) : -1;
        uint32_t w0 = ph >= 0 ? (uint32_t)ph / 16u * 16u : seq_pg[t] * 16u;
        char *ti = title[t], nm[PATCH_NAME + 1];
        for (k = 0; k < PATCH_NAME; k++) nm[k] = (char)d->v[NPARAM + k];
        nm[PATCH_NAME] = 0;
        for (k = PATCH_NAME; k && nm[k - 1u] == ' '; k--) nm[k - 1u] = 0;
        ti[0] = 0;
        if (trk_prog[t] < 9u) scat(ti, "0");
        fmt_u(ti + slen(ti), trk_prog[t] + 1u); scat(ti, " "); scat(ti, nm);
        if (trk_edited[t]) scat(ti, "*");
        if (rec == REC_ON && t == seq_trk)      /* the track REC writes: REC, or NEXT while it waits for the bar */
            ui_seq_track_row(t, 1, ti, C_CREAM, rec_wait ? "NEXT" : "REC", C_RED, w0, ph);
        else
        {
        static char bb[NTRK][8];                /* playing, longer than a bar: which bar of how many */
        const char *stt = ST[st];
        if (st == 1u && ph >= 0 && len > 16u) { bb[t][0] = 0; fmt_u(bb[t], (uint32_t)ph / 16u + 1u); scat(bb[t], "/"); fmt_u(bb[t] + slen(bb[t]), (len + 15u) / 16u); stt = bb[t]; }
        ui_seq_track_row(t, t == seq_trk, ti, len ? C_CREAM : C_DIMT, trk_silent(t) ? "MUTE" : !set.mix_lvl[t] ? "LVL 0" : len ? stt : "OFF",
                         !set.mix_lvl[t] ? C_RED : st >= 2u ? C_AMBER : st ? C_GREEN : C_DIMT, w0, ph);
        }
    }
}
static void seq_tracks_line(char *s, uint16_t *c)
{
    uint32_t t = seq_trk % NTRK;
    s[0] = 0;
    *c = C_TRKC[t];
    if (toast && fm1_ms - toast_t < 1500u) { scat(s, toast); return; }
    if (rec_wait) { *c = C_RED; scat(s, "TRACK "); s[6] = TRK_L[t]; s[7] = 0; scat(s, " RECORDS ON THE BAR"); return; }
    if (rec) { *c = C_RED; scat(s, rec == REC_ON ? "RECORDING " : ctl_seq_run ? "REC ARMED " : "REC READY "); { char l[2] = {TRK_L[t], 0}; if (rec == REC_ON) scat(s, l); }
               scat(s, rec == REC_ON ? "  GLO+1-4" : "  GLO+1-4: RECORD"); return; }
    *c = C_DIMT;
    { char l[2] = {TRK_L[t], 0}; scat(s, l); }
    scat(s, ": ");
    if (pat.trk[t].len) { fmt_u(s + slen(s), pat.trk[t].len); scat(s, " "); scat(s, DIV_NAME[pat.trk[t].div % NDIV]); }
    else scat(s, "OFF");
    scat(s, "  GLO+1-4: START");
}

static void draw_seq_slots(void)
{
    static char txt[NBLOB][16], l1[40], l2[32], l3[32];
    const char *p[NBLOB];
    uint32_t i;
    uint16_t c = C_AMBER;
    for (i = 0; i < NPAT; i++) {
        uint32_t t;
        txt[i][0] = 0;
        if (!(pat_used >> i & 1u) && i != set.pat) scat(txt[i], "EMPTY");
        else {                                  /* how many tracks are on, the longest: 4 TRK 16 */
            uint32_t on = 0, mx = 0;
            for (t = 0; t < NTRK; t++) {
                uint32_t l = i == set.pat ? pat.trk[t].len : slot_len[i][t];
                if (l) { on++; if (l > mx) mx = l; }
            }
            fmt_u(txt[i], on); scat(txt[i], " TRK "); fmt_u(txt[i] + slen(txt[i]), mx);
        }
        p[i] = txt[i];
    }
    ui_seq_slots(p, pat_used, slot_sel, set.pat, pat_edited);
    l1[0] = l2[0] = l3[0] = 0;
    if (toast && fm1_ms - toast_t < 1500u) scat(l1, toast);
    else {
        scat(l1, "NOW: PATTERN "); fmt_u(l1 + slen(l1), set.pat + 1u); scat(l1, pat_edited ? "*" : "");
        if (pat_queue) { scat(l1, "  NEXT: "); fmt_u(l1 + slen(l1), pat_queue); }
        c = C_CREAM;
    }
    scat(l2, NM_K1 " SLOT  " NM_K3 " GO  " NM_K4 " COPY HERE");
    scat(l3, NM_PRESETS ": PATTERN  " NM_SAVE ": SAVE");
    ui_seq_lines(l1, c, l2, l3);
}

static void draw_seq_knobs(void)
{
    knob_view_t k[4];
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        k[i].value[0] = 0; k[i].bip = 0; k[i].present = 0;
        if (layer == L_SEQ || step_held >= 0) seq_knob(&k[i], i);
        else if (layer) layer_knob(&k[i], i);
        else seq_view_knob(&k[i], i);
    }
    ui_trk = seq_trk;
    ui_seq_knobs(k);
}

/* ---- the main loop */
#define KEY0 53u                               /* key 0 = F3 */
/* the keys and MIDI in: read by the main loop, and while the screen is drawn (app_idle, notes only), so the notes
 * of a chord or among a stream of CCs are not held up by a redraw (a knob page is ~30 ms on the SPI alone) */
static uint32_t dirty_hdr = 1, dirty_page = 1, dirty_tags = 1, dirty_steps = 1;
static uint32_t keys_prev, clk_n, clk_t0, clk_last, idle_ok;
static uint8_t held_key[27], key_step[27];
static int notes_shown(void) { return seq_shown() || rec; }   /* the only screens a note changes */

/* a note from the keys or MIDI on the chosen track: it sounds, REC takes it, the sequencer's views use it */
static void note_in(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t t;
    note &= 127u;
    play_note(on, note, vel);
    if (!on) for (t = 0; t < NTRK; t++) ctl_live_hold[t][note >> 5 & 3u] &= ~(1u << (note & 31u));   /* (let go: the sequencer's again) */
    if (rec_keys()) rec_note(on, note, vel);
    if (on) {
        pen_add(note);
        if (step_held < 0) entry_note(1, note, vel);
        else if (!rec) {                        /* a held step takes what is played: the first note starts the chord */
            step_t *sp = seq_step(seq_trk, (uint32_t)step_held);
            uint32_t q;
            if (!step_midi++) { for (q = 0; q < SEQ_CHORD; q++) sp->note[q] = 0; sp->ratch &= (uint8_t)~STEP_OFF; }
            for (q = 0; q < SEQ_CHORD && sp->note[q] && sp->note[q] != note; q++) ;
            if (q < SEQ_CHORD && !sp->note[q]) { sp->note[q] = (uint8_t)note; sp->vel = (uint8_t)vel; }
            pat_edited = 1;
        }
    } else {
        if (notes_down) notes_down--;
        entry_note(0, note, 0);
    }
}

#if BOARD_SMK37
#include "obv_smk.h"                            /* the SMK-37's pads, outer keys, faders, wheels, < >, FADER BANK */
#endif

static uint32_t cycle_page(uint32_t first, uint32_t count)   /* a button steps through its pages */
{
    if (page >= first && page < first + count) return first + (page - first + 1u) % count;
    return first;
}

static void keys_in(uint32_t notes_only)
{
    uint32_t keys = fm1_in.notes, down = keys & ~keys_prev, up = keys_prev & ~keys, base = (uint32_t)(KEY0 + 12 * set.oct), k;
    for (k = 0; k < 27u; k++) {             /* the keys by layer: EDIT erases, SEQ steps, GLO mix, SAVE patterns; else notes */
        if (down >> k & 1u) {
            uint32_t ks = layer == L_EDIT ? 2u : steps_on() ? 1u : layer == L_GLO ? 3u : layer == L_SAVE ? 4u : 0u;
            if (notes_only && ks) continue;     /* not a note: the main loop's (its bit stays new) */
            keys_prev |= 1u << k;
            held_key[k] = (uint8_t)(base + k);
            key_step[k] = (uint8_t)ks;
            if (key_step[k] == 2u) {        /* EDIT held: the key erases its note */
                if (!layer_used) undo_save();
                layer_used = 1;
                erase_note(held_key[k]);
                say("ERASED");
            } else if (key_step[k] == 1u) seq_key_down(k);
            else if (key_step[k] == 3u) { glo_key(k); layer_used = 1; }
            else if (key_step[k] == 4u) {   /* SAVE held: white keys 1-8 = pattern 1-8 */
                static char msg[16];
                layer_used = 1;
                if (KEY_STEP[k] >= 0 && KEY_STEP[k] < (int32_t)NPAT) {   /* white 1-8: go there; black 1-8: copy there */
                    if ((uint32_t)KEY_STEP[k] != set.pat || pat_queue) pat_go((uint32_t)KEY_STEP[k]);
                    else { msg[0] = 0; scat(msg, "PATTERN "); fmt_u(msg + slen(msg), set.pat + 1u); say(msg); }
                } else if (KEY_PAGE[k] >= 0) pat_copy((uint32_t)KEY_PAGE[k]);
            } else {
                midi_send(0x90u | MIDI_OUT_CH, held_key[k], KEY_VEL(k));
                note_in(1, held_key[k], KEY_VEL(k));
                if (notes_shown()) dirty_steps = dirty_page = 1;
                continue;
            }
            dirty_steps = dirty_page = 1;
        }
        if (up >> k & 1u) {
            if (notes_only && key_step[k]) continue;
            keys_prev &= ~(1u << k);
            if (key_step[k] == 2u) erase_on[(uint32_t)clampi((int32_t)held_key[k] - pat.trk[seq_trk].transp, 1, 127)] = 0;
            else if (key_step[k] == 1u) seq_key_up(k);
            else if (key_step[k] == 0u) {
                midi_send(0x80u | MIDI_OUT_CH, held_key[k], 0);
                note_in(0, held_key[k], 0);
                if (notes_shown()) dirty_steps = dirty_page = 1;
                continue;
            }
            dirty_steps = dirty_page = 1;
        }
    }
}

static uint8_t midi_back[4];                    /* a message left for the main loop (app_idle met a program change) */
static void midi_in(uint32_t notes_only)
{
    uint8_t st, d1, d2;
    for (;;) {
        uint32_t ch, kind;
        if (midi_back[3]) { st = midi_back[0]; d1 = midi_back[1]; d2 = midi_back[2]; midi_back[3] = 0; }
        else if (!midi_get(&st, &d1, &d2)) break;
        if (notes_only && (st & 0xF0u) == 0xC0u) {   /* a program change loads a sound: not in the middle of a draw */
            midi_back[0] = st; midi_back[1] = d1; midi_back[2] = d2; midi_back[3] = 1;
            break;
        }
        ch = (st & 15u) + 1u; kind = st & 0xF0u;
        if (set.sync_int && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) continue;   /* (SYNC INTERNAL) */
        if (st == 0xF8u) {                  /* clock: 24 per quarter. The tempo from 4 quarters (96 clocks) in rendered
                                             * frames, rounded to 48 as our own (quarter_len); taken only when it moves
                                             * by more than 0.3 %: the clock's jitter must not wobble the steps */
            if (!clk_n || fm1_ms - clk_last > 250u) { clk_n = 0; clk_t0 = eng_t; }
            clk_last = fm1_ms;
            if (++clk_n == 97u) {
                uint32_t q = ((eng_t - clk_t0) / 4u + 24u) / 48u * 48u, d;
                if (q > FS / 5u && q < FS * 8u / 5u) {
                    d = q > ctl_ext_quarter ? q - ctl_ext_quarter : ctl_ext_quarter - q;
                    if (!ctl_ext_quarter || d * 1000u > 3u * q) ctl_ext_quarter = q;
                }
                clk_n = 1; clk_t0 = eng_t;
            }
            continue;
        }
        if (st == 0xFAu || st == 0xFBu) {   /* start / continue: the arp and the sequencer from the top */
            ctl_arp_restart = 1; clk_n = 0;
            ctl_seq_restart = 1; ctl_seq_run = 1;
            dirty_tags = dirty_steps = 1;
            continue;
        }
        if (st == 0xFCu) { ctl_seq_run = 0; dirty_tags = dirty_steps = 1; continue; }
        if (st >= 0xF0u) continue;
        if (set.midi_ch && ch > set.midi_ch && ch < set.midi_ch + NTRK) {   /* the next three channels: tracks B-D */
            uint32_t t = ch - set.midi_ch;
            if (kind == 0x90u && d2) note_event_p(t, 1, d1, d2);
            else if (kind == 0x80u || kind == 0x90u) note_event_p(t, 0, d1, 0);
            else if (kind == 0xC0u && d1 < NPROG) { trk_load(t, d1); dirty_hdr = dirty_page = dirty_tags = dirty_steps = 1; }
            else if (kind == 0xB0u && cc_map[d1]) {
                uint32_t p = cc_map[d1] - 1u;
                if (!is_a_only(p)) set_param_in(&live_p[t], p, PDEF[p].max == 127 ? (int32_t)d2 : ((int32_t)d2 * PDEF[p].max + 63) / 127);
                dirty_page = dirty_hdr = 1;
            }
            continue;
        }
        if (set.midi_ch && ch != set.midi_ch) continue;
        if (kind == 0x90u && d2) {
            note_in(1, d1, d2);
            if (notes_shown()) dirty_steps = dirty_page = 1;
        }
        else if (kind == 0x80u || kind == 0x90u) {
            note_in(0, d1, 0);
            dirty_page |= rec || is_seq_page(page); dirty_steps = 1;
        }
        else if (kind == 0xE0u) ctl_bend = (int32_t)(d1 | d2 << 7) - 8192;
        else if (kind == 0xC0u && d1 < NPROG) { trk_load(set.midi_ch ? 0u : seq_trk, d1); dirty_hdr = dirty_page = dirty_tags = dirty_steps = 1; }
        else if (kind == 0xB0u) {
            if (d1 == 7u) ctl_cc7 = d2;
            else if (d1 == 64u) ctl_sustain = d2 >= 64u;
            else if (d1 == 120u || d1 == 123u) ctl_panic = 1;
            else if (cc_map[d1]) {
                uint32_t p = cc_map[d1] - 1u;
                set_param(p, PDEF[p].max == 127 ? (int32_t)d2 : ((int32_t)d2 * PDEF[p].max + 63) / 127);
                auto_rec(p);
                if (p == P_CUTOFF) cut_shown_t = fm1_ms + 1u;
                dirty_page = dirty_hdr = 1;
                if (p == P_MODE || p == P_FTYPE || p == P_ARP_MODE) dirty_tags = 1;
            }
        }
    }
}

static void app_idle(void)                      /* lcd.c while a strip goes out, ui_page between the knobs: the notes */
{
    static uint32_t busy;
    if (!idle_ok || busy) return;
    busy = 1;
    keys_in(1);
    midi_in(1);
    busy = 0;
}

/* ---- the tracks: chosen (ALGORITHM), started / stopped on the bar and recorded (GLO held + keys 1-4), cleared (REC held) */
static uint32_t req_page;                       /* a page asked for + 1: taken by the main loop */
static void trk_select(uint32_t t)
{
    static char msg[20];
    t %= NTRK;
    if (t != seq_trk) { seq_trk = t; step_held = -1; audition_off(); entry_reset(); }
    msg[0] = 0; scat(msg, "TRACK "); msg[6] = TRK_L[t]; msg[7] = 0;
    if (!pat.trk[t].len) scat(msg, is_seq_page(page) && seq_view == SV_TRACKS ? " OFF: " NM_K3 " = ON" : " OFF: " NM_K1 " = ON");
    say(msg);
    dirty_page = dirty_steps = dirty_tags = dirty_hdr = 1;
}
static void trk_launch(uint32_t t)              /* start / stop on the next bar (stopped: it starts, and the transport with it) */
{
    static char msg[24];
    uint32_t will;
    t %= NTRK;
    if (!ctl_seq_run) {                         /* stopped: a pad always starts (its track on, the transport from the top) */
        if (!((ctl_trk_on ^ trk_pending()) >> t & 1u)) ctl_trk_req ^= 1u << t;
        ctl_seq_restart = 1; ctl_seq_run = 1;
        msg[0] = TRK_L[t]; msg[1] = 0; scat(msg, " PLAYS"); say(msg);
        dirty_steps = dirty_tags = 1;
        return;
    }
    ctl_trk_req ^= 1u << t;
    will = (ctl_trk_on ^ trk_pending()) >> t & 1u;
    msg[0] = TRK_L[t]; msg[1] = 0;
    scat(msg, !ctl_seq_run ? (will ? " PLAYS" : " STOPPED") : trk_pending() >> t & 1u ? (will ? " STARTS ON THE BAR" : " STOPS ON THE BAR") : (will ? " PLAYS" : " STOPPED"));
    say(msg);
    dirty_steps = dirty_tags = 1;
}
/* REC lit + GLO held + key 1-4: that track records (Ableton's session record). Playing: from now; stopped or starting:
 * it starts (stopped transport: at once, from the top, with the count-in) and records from its start on the bar.
 * Its own pad again: the take ends, the track plays on, REC stays armed (REC itself switches it off). Another track's pad: the recording moves there. */
static void trk_record(uint32_t t)
{
    static char msg[28];
    uint32_t will;
    t %= NTRK;
    msg[0] = TRK_L[t]; msg[1] = 0;
    if (rec == REC_ON && seq_trk == t) {
        scat(msg, rec_wait ? " NOT RECORDED" : " RECORDED");
        rec = REC_READY; rec_wait = 0;          /* (the take ends, REC stays armed: the next pad records; REC: off) */
        memset(rec_open, 0, sizeof rec_open);
        say(msg); dirty_steps = dirty_tags = dirty_page = 1;
        return;
    }
    memset(rec_open, 0, sizeof rec_open);
    rec_wait = 0;
    if (!pat.trk[t].len) { pat.trk[t].len = 16; pat_edited = 1; }   /* (an empty track: a bar of sixteenths) */
    trk_select(t);
    will = (ctl_trk_on ^ trk_pending()) >> t & 1u;
    if (!will) ctl_trk_req ^= 1u << t;          /* stopped, or stopping on the bar: it plays (again) */
    if (!ctl_seq_run) {                         /* as PLAY with REC armed */
        rec_begin();
        ctl_count = set.click % NCLICK == CLICK_COUNT;
        ctl_seq_restart = 1; ctl_seq_run = 1;
        scat(msg, " RECORDING");
    } else if (trk_pending() >> t & 1u) {       /* starts on the bar: REC with it */
        rec_begin(); rec_wait = t + 1u;
        scat(msg, " RECORDS ON THE BAR");
    } else {
        rec_begin();
        scat(msg, " RECORDING");
    }
    say(msg);
    dirty_steps = dirty_tags = dirty_page = 1;
}
/* the chosen track emptied (REC held 1.2 s), its recorded knobs too; one undo. A take on it goes on armed */
static void trk_clear(void)
{
    static char m[20];
    uint32_t n;
    undo_save();
    for (n = 0; n < pat.trk[seq_trk].len; n++) step_clear(seq_step(seq_trk, n));
    for (n = 0; n < NLANE; n++) lane_clear(&pat.lane[seq_trk % NTRK][n]);
    pat_edited = 1;
    if (rec == REC_ON) { rec = ctl_seq_run ? REC_READY : REC_OFF; rec_wait = 0; }
    memset(rec_open, 0, sizeof rec_open);
    m[0] = 0; scat(m, "TRACK "); m[6] = TRK_L[seq_trk % NTRK]; m[7] = 0; scat(m, " CLEARED"); say(m);
    dirty_tags = dirty_steps = dirty_page = 1;
}
static void app_main(void)
{
    uint32_t k, i, t_led = 0, t_scope = 0, t_page = 0, t_hdr = 0, leds_q = 1;
    uint32_t swap_seen = 0, leds_prev = 0xFFFFFFFFu, glow = 0, glow_prev = 0xFFFFFFFFu, t_steps = 0, ticks_seen = 0;
    rnd = 0xC0FFEE42u;
#if BOARD_SMK37
    smk_pad_events = 1;                         /* the pads are OberVave's step pads (obv_smk.h), not notes */
#endif
    engine_init();
    cc_map_init();
    {
        uint32_t n;
        for (n = 0; n < NPSECT; n++)
            if (psect_load(n, &psect[n], sizeof psect[n]) || psect[n].tag != PS_TAG) {
                memset(&psect[n], 0, sizeof psect[n]);
                psect[n].tag = PS_TAG;
            }
        set.version = 0;
        if (settings_load(&set, sizeof set) || set.version != SET_VERSION) {
            static set_v2_t old __attribute__((section(".pool")));
            set_v3_t o3;
            memset(&set, 0, sizeof set);
            set.version = SET_VERSION;
            set.tempo = 120;
            set.click = CLICK_REC;              /* (the click while recording, from the start) */
            old.version = 0; o3.version = 0;
            if (!settings_load(&o3, sizeof o3) && o3.version == 3u) {   /* 1.5.x: its globals; its patterns come over by themselves */
                set.prog = o3.prog; set.tempo = o3.tempo; set.midi_ch = o3.midi_ch;
                set.tune = o3.tune; set.oct = o3.oct; set.pat = o3.pat; set.click = o3.click;
                set_dirty = 1;
            } else if (!settings_load(&old, sizeof old) && old.version == 2u) {   /* 1.4.x: its globals and 32 programs move */
                set.prog = old.prog; set.tempo = old.tempo; set.midi_ch = old.midi_ch;
                set.tune = old.tune; set.oct = old.oct; set.pat = old.pat; set.click = old.click;
                for (n = 0; n < 32u; n++)
                    if (old.used >> n & 1u) prog_put(n, &old.user[n]);
                set_dirty = 1;                  /* (the programs are written first, then these) */
            }
        }
    }
    if (!set.mix_lvl[0] && !set.mix_lvl[1] && !set.mix_lvl[2] && !set.mix_lvl[3]) {   /* (new: the tracks' sounds and the mixer) */
        uint32_t t;
        for (t = 0; t < NTRK; t++) { set.tprog[t] = TPROG_DEF[t]; set.mix_lvl[t] = 100; set.mix_cho[t] = set.mix_dly[t] = set.mix_rev[t] = 0; set.mix_pan[t] = 0; }
        /* track A into the effects as OberVave always was (its program's chorus, delay, reverb); B-D are sent on purpose */
        set.mix_cho[0] = set.mix_dly[0] = set.mix_rev[0] = 127;
        set.fx_ok = 0;                          /* (fx_sync below takes them from track A's program) */
        set.tprog[0] = set.prog;
        set.mix_mute = set.mix_solo = 0;
        set.rec_q = RQ_16;
    }
    if (set.rec_qt_ok != RQT_OK) {              /* (settings from before: every track gets the one quantise there was) */
        uint32_t t;
        for (t = 0; t < NTRK; t++) set.rec_qt[t] = (uint8_t)(set.rec_q % NRQ);
        set.rec_qt_ok = RQT_OK;
    }
    if (set.dv_ok != DV_OK) { uint32_t t; for (t = 0; t < NTRK; t++) set.mix_drv[t] = 0; set.dv_type = DV_OVERDRIVE; set.dv_amt = 60; set.dv_tone = 90; set.dv_mix = 127; set.dv_ok = DV_OK; }
    if (set.m_ok != M_OK) { set.m_vol = 100; set.m_dj = 64; set.m_drive = 0; set.m_comp = 0; set.m_ok = M_OK; }
    if (set.prog >= NPROG) set.prog = 0;
    if (set.tempo < 40u || set.tempo > 240u) set.tempo = 120;
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.midi_ch > 16u) set.midi_ch = 0;
    ctl_tempo = set.tempo;
    ctl_tune8 = set.tune * 256 / 100;
    {
        uint32_t t;
        for (t = 0; t < NTRK; t++) trk_load(t, set.tprog[t] < NPROG ? set.tprog[t] : 0u);
        fx_sync();                              /* (the first start: track A's sound's effects become the song's) */
        set_dirty = 0;
    }
    mix_apply();
    if (set.pat >= NPAT) set.pat = 0;
    slot_lens_read();                           /* (every pattern's sounds; 1.5's patterns converted) */
    load_pattern(set.pat);
    slot_sel = set.pat;
    ctl_panic = 0;

    ui_splash();
    scope_state = 1;

    for (;;) {
        uint32_t rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct), np = page;
        int32_t e;
        if (mirror_poll()) {                    /* FM-1 Mirror opened (or rows were lost): everything drawn again */
            lcd_fill(0, 0, 240, 240, C_PANEL);
            dirty_hdr = dirty_page = dirty_steps = dirty_tags = 1; hints_key = leds_q = 0xFFFFFFFFu; scope_state = 1; t_scope = 0;
        }
        platform_poll();

        keys_in(0);
#if BOARD_SMK37
        smk_pads(base, &dirty_steps);           /* the step pads, the outer keys */
        if (dirty_steps) dirty_page = 1;
#endif
        take_check();
        while (arp_qr != arp_qw) {              /* REC with the arp on: what it plays is recorded, where it played it */
            arpev_t ev;
            RING_TAKE();
            ev = arp_q[arp_qr % AQ];
            arp_qr++;
            if (ev.part == seq_trk && ed_patch(P_ARP_MODE)->v[P_ARP_MODE]) {
                rec_from_arp = 1; rec_arp_q8 = ev.q8;
                rec_note(ev.on, ev.note, ev.vel ? ev.vel : 100u);
                rec_from_arp = 0; rec_arp_q8 = 0xFFFFFFFFu;
                dirty_steps = 1;
            }
        }
        auto_poll();
        if (ctl_seq_run && pat.trk[seq_trk].len && ui_seq_pos[seq_trk] != erase_pos) {   /* erasing as the playhead passes */
            uint32_t n;
            erase_pos = ui_seq_pos[seq_trk];
            for (n = 0; n < 128u; n++)
                if (erase_on[n]) step_remove(seq_step(seq_trk, erase_pos % pat.trk[seq_trk].len), n);
        }

        /* buttons, as sloopDX: tapped = its pages, held = a layer, HOME while held = locked open (any layer button
         * then only lets it go); PLAY, REC, OCT work inside a layer */
        b = fm1_input_edges(&rel);
        if (b & ~(1u << BTN_SAVE)) pat_save_armed = 0;   /* (SAVE PATTERN?: any other button says no) */
        {
            uint32_t mh = is_seq_page(page) && seq_view == SV_STEPS && !layer && (fm1_in.buttons >> BTN_SEL & 1u);
            if (mh != more_held) { more_held = mh; dirty_page = dirty_steps = 1; }   /* (SELECT held in STEPS: STEP+) */
        }
        if (b & (1u << BTN_SEL) && page == PG_GLOBAL && !layer) {   /* GLOBAL: SELECT pressed = SYNC AUTO / INTERNAL */
            set.sync_int = !set.sync_int; set_dirty = 1; set_t = fm1_ms;
            if (set.sync_int) { ctl_ext_quarter = 0; clk_n = 0; }
            say(set.sync_int ? "SYNC INTERNAL" : "SYNC AUTO: A MIDI CLOCK LEADS");
            dirty_page = 1;
        }
        {
            static const uint8_t LB[5] = {BTN_SEQ, BTN_EDIT, BTN_GLO, BTN_SAVE, BTN_FX};
            static const uint8_t LL[5] = {L_SEQ, L_EDIT, L_GLO, L_SAVE, L_FX};
            static uint32_t dead;               /* a press that only unlocked: its release does nothing */
            for (k = 0; k < 5u; k++) {
                if (b & (1u << LB[k])) {
                    if (layer_lock) { layer = L_NONE; layer_lock = 0; dead |= 1u << LB[k]; }
                    else { layer = LL[k]; layer_used = 0; layer_t = fm1_ms; layer_btn = LB[k]; }
                    step_held = -1; audition_off(); entry_reset();
                    dirty_page = dirty_steps = dirty_tags = 1; hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
                }
                if (rel & (1u << LB[k])) {
                    if (dead & (1u << LB[k])) { dead &= ~(1u << LB[k]); continue; }
                    if (layer != LL[k] || layer_lock) continue;
                    if (!layer_used && fm1_ms - layer_t < 600u) {   /* a tap: the button's pages */
                        if (LL[k] == L_SEQ) {   /* SEQ: the full-screen sequencer; there, its next view (as SLOOP) */
                            static const char *const HINT[NSV] = {NM_ALGO ": TRACK A-D", "KEYS WRITE  " NM_SELBTN " HELD: MORE", "MORE PER STEP", "THE TRACK: LENGTH ..", "THE 8 PATTERNS"};
                            if (is_seq_page(page)) { seq_view = (seq_view + 1u) % NSV; if (seq_view == SV_MORE) seq_view = SV_TRACK; }   /* (STEP+ = SELECT held in STEPS) */
                            else { seq_back = page; np = PG_SEQ; seq_view = SV_TRACKS; }
                            if (seq_view == SV_SLOTS) slot_sel = pat_queue ? pat_queue - 1u : set.pat;
                            say(HINT[seq_view]);
                            entry_reset();
                        } else if (LL[k] == L_EDIT && is_seq_page(page) && SV_ENTRY(seq_view)) seq_clear_cursor();
                        else if (LL[k] == L_EDIT) np = cycle_page(PG_OSC1, 6);
                        else if (LL[k] == L_FX) np = cycle_page(PG_CHORUS, 4);
                        else if (LL[k] == L_GLO) np = page == PG_GLOBAL ? PG_OUTPUT : PG_GLOBAL;
                        else if (is_seq_page(page)) {   /* in the sequencer: SAVE PATTERN? - SAVE again saves (as zp12:
                                                 * now; playing, at the stop), anything else: no */
                            if (pat_save_armed && fm1_ms - pat_save_t < 3000u) { pat_save_armed = 0; pat_save_to(set.pat); }
                            else { static char m[24]; pat_save_armed = 1; pat_save_t = fm1_ms; m[0] = 0; scat(m, "SAVE PATTERN "); fmt_u(m + slen(m), set.pat + 1u); scat(m, "? " NM_SAVE); say(m); }
                            entry_reset();
                        } else if (save_armed) {      /* SAVE tapped twice: into the slot (PRESETS in between: another) */
                            uint32_t n = save_to % NPROG, t = seq_trk % NTRK, u;
                            patch_t sp = live_p[t];
                            prog_put(n, &sp);
                            trk_prog[t] = n; trk_edited[t] = 0; set.tprog[t] = (uint8_t)n; if (!t) set.prog = (uint8_t)n;
                            set_dirty = 1;
                            for (u = 0; u < NTRK; u++)  /* (another track on the same program, unchanged: it hears it too) */
                                if (u != t && trk_prog[u] == n && !trk_edited[u]) { live_p[u] = sp; if (!u) fx_keep(&live_p[0]); }
                            save_armed = 0;
                            say("SAVED");
                            dirty_hdr = dirty_tags = 1;
                        } else { save_armed = 1; save_t = fm1_ms; save_to = ed_prog(); dirty_hdr = 1; }
                    }
                    layer = L_NONE;
                    memset(erase_on, 0, sizeof erase_on);
                    step_held = -1; audition_off();
                    dirty_page = dirty_steps = dirty_tags = 1; hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
                }
            }
        }
#if BOARD_SMK37
        smk_buttons(b, &dirty_steps);           /* FADER BANK, < > */
#endif
        ctl_click = set.click % NCLICK == CLICK_ON || (rec == REC_ON && set.click % NCLICK != CLICK_OFF);
        if (b & (3u << BTN_OCT_DOWN)) {
            if (layer == L_EDIT) {              /* EDIT + OCT- / OCT+: undo / redo (one level: a swap) */
                if (undo_ok) { pattern_t tmp = pat; pat = undo_pat; undo_pat = tmp; pat_edited = 1; say(b & (1u << BTN_OCT_DOWN) ? "UNDO" : "REDO"); }
                else say("NOTHING TO UNDO");
                layer_used = 1;
            } else if (steps_on()) {            /* the SEQ layer: OCT- / OCT+ page */
                uint32_t t = seq_trk;
                static char msg[20];
                layer_used = 1;
                if (b & (1u << BTN_OCT_DOWN) && seq_pg[t]) seq_pg[t]--;
                if (b & (1u << BTN_OCT_UP) && seq_pg[t] + 1u < seq_pages(t)) seq_pg[t]++;
                msg[0] = 0; scat(msg, "PAGE "); fmt_u(msg + slen(msg), seq_pg[t] + 1u); scat(msg, " OF "); fmt_u(msg + slen(msg), seq_pages(t));
                say(msg);
            } else if ((fm1_in.buttons & (3u << BTN_OCT_DOWN)) == (3u << BTN_OCT_DOWN)) {   /* both: octave 0 */
                set.oct = 0; set_dirty = 1; set_t = fm1_ms; say("OCTAVE 0");
            } else {
                if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; set_dirty = 1; set_t = fm1_ms; }
                if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; set_dirty = 1; set_t = fm1_ms; }
            }
            dirty_steps = dirty_page = 1;
        }
        if (b & (1u << BTN_HOME)) {             /* HOME: PERFORM; while a layer is held: lock it open; locked: let it go */
            if (layer && !layer_lock) { layer_lock = 1; layer_used = 1; say("LOCKED: ITS BUTTON ENDS IT"); }
            else if (layer_lock) { layer = L_NONE; layer_lock = 0; dirty_page = dirty_steps = dirty_tags = 1; leds_q = 0xFFFFFFFFu; }
            else if (page == PG_PERFORM) { np = PG_MIXER; mix_row = 0; }   /* again: the MIXER (VIBE's F1) */
            else if (page == PG_MIXER) {        /* there: its next row */
                static const char *const MR[7] = {"LEVEL", "PAN", "SEND CHORUS", "SEND DELAY", "SEND REVERB", "SEND DRIVE", "MASTER"};
                mix_row = (mix_row + 1u) % 7u; say(MR[mix_row]); dirty_page = dirty_steps = 1;
            }
            else np = PG_PERFORM;
        }
        if (b & ((1u << BTN_ENV) | (1u << BTN_LFO) | (1u << BTN_ARP)) && layer_lock) {   /* (another button lets a lock go) */
            layer = L_NONE; layer_lock = 0; b &= ~((1u << BTN_ENV) | (1u << BTN_LFO) | (1u << BTN_ARP));
            dirty_page = dirty_steps = dirty_tags = 1; leds_q = 0xFFFFFFFFu;
        }
        if (b & (1u << BTN_ENV)) np = cycle_page(PG_FENV, 2);
        if (b & (1u << BTN_LFO)) np = cycle_page(PG_LFO, 3);
        if (b & (1u << BTN_ARP)) {
            if (page == PG_ARP) { hold ^= 1u << (seq_trk % NTRK); ctl_hold = hold; dirty_tags = 1; }   /* on its page: the panel's sound's HOLD */
            else np = PG_ARP;
        }
        if (b & (1u << BTN_PLAY)) {             /* start from step 1 / stop (a take ends with it) */
            if (ctl_seq_run) { ctl_seq_run = 0; rec = REC_OFF; rec_wait = 0; }   /* (the stop ends REC, armed too: the pads start / stop again) */
            else {
                if (rec == REC_READY) { rec = REC_ON; undo_save(); ctl_count = set.click % NCLICK == CLICK_COUNT; }
                ctl_seq_restart = 1; ctl_seq_run = 1;
            }
            dirty_tags = dirty_steps = 1;
        }
        if (b & (1u << BTN_REC)) { rec_down = 1; rec_down_t = fm1_ms; }   /* REC acts when let go; held 1.2 s it clears the track */
        if (rec_down && fm1_ms - rec_down_t > 1200u) {
            trk_clear();
            rec_down = 0;
            leds_q = 0xFFFFFFFFu;
        }
        if (rel & (1u << BTN_REC) && rec_down) {
            if (fm1_ms - rec_down_t < 500u) {   /* a press: playing, record now / stop recording; stopped, arm */
                if (rec) rec = REC_OFF;
                else if (ctl_seq_run) { rec = REC_ON; undo_save(); }
                else rec = REC_READY;
                memset(rec_open, 0, sizeof rec_open);
            }
            rec_down = 0;
            hints_key = 0xFFFFFFFFu;
            leds_q = 0xFFFFFFFFu;
            dirty_tags = dirty_steps = dirty_page = 1;
        }
        if (b & (1u << BTN_SEL) && layer == L_SAVE) {   /* SAVE held + SELECT pressed: a new sound (dice) */
            randomize();
            layer_used = 1;
            save_armed = 0;
            dirty_hdr = 1;
            dirty_hdr = dirty_tags = dirty_page = 1;
        }
        if (save_armed && fm1_ms - save_t > 3000u) { save_armed = 0; dirty_page = dirty_hdr = 1; }
        fx_sync();
        if (rec_wait) {                         /* REC waits for a track's bar: there it is */
            uint32_t w = rec_wait - 1u;
            if (rec != REC_ON || seq_trk != w) rec_wait = 0;
            else if (ctl_trk_on >> w & 1u && !(trk_pending() >> w & 1u)) { rec_wait = 0; memset(rec_open, 0, sizeof rec_open); dirty_steps = dirty_tags = 1; }
        }
        if (req_page) { np = req_page - 1u; req_page = 0; if (np == page) dirty_page = dirty_steps = dirty_tags = 1; }
        if (np != page) {
            if (layer_lock) { layer = L_NONE; layer_lock = 0; }
            if (is_seq_page(np) != is_seq_page(page)) {   /* into / out of the full-screen sequencer: all new */
                lcd_fill(0, 0, 240, 240, C_PANEL);
                dirty_hdr = dirty_tags = 1; scope_state = 1; t_scope = 0;
            } else if ((np == PG_MIXER) != (page == PG_MIXER)) {   /* into / out of the mixer: its strip or the scope's */
                lcd_fill(0, 146, 240, 94, C_PANEL);
                scope_state = 1; t_scope = 0; leds_q = 0xFFFFFFFFu;
            }
            entry_reset();
            page = np; dirty_page = dirty_steps = 1; step_held = -1; audition_off(); hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
        }

        /* encoders */
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0 && save_armed) {           /* SAVE armed: PRESETS picks the slot to save into ("save as") */
            save_to = (uint32_t)((int32_t)save_to + e + (int32_t)NPROG * 4) % NPROG;
            save_t = fm1_ms;
            dirty_hdr = 1;
        } else if (e) {                         /* the chosen track's sound */
            trk_load(seq_trk, (uint32_t)((int32_t)trk_prog[seq_trk % NTRK] + e + (int32_t)NPROG * 4) % NPROG);
            dirty_hdr = dirty_page = dirty_tags = dirty_steps = 1;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {   /* ALGORITHM: the track A-D (as sloopDX) */
            trk_select((uint32_t)clampi((int32_t)seq_trk + (e > 0 ? 1 : -1), 0, NTRK - 1));
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {   /* SELECT (VIBE's DATA): STEPS the bar, the sequencer's other views and
                                                 * the MIXER the next pattern (on the bar), else the tempo */
            static char msg[16];
            uint32_t t = seq_trk % NTRK, len = pat.trk[t].len;
            if (is_seq_page(page) && SV_ENTRY(seq_view) && !layer) {
                if (len) {
                    int32_t c = (int32_t)(seq_cur[t] % len) + (e > 0 ? 16 : -16);
                    seq_cursor_set(t, (uint32_t)(c < 0 ? (c + (int32_t)len * 16) % (int32_t)len : c % (int32_t)len));
                    msg[0] = 0; scat(msg, "BAR "); fmt_u(msg + slen(msg), seq_cur[t] / 16u + 1u); scat(msg, " OF "); fmt_u(msg + slen(msg), seq_pages(t));
                    say(msg);
                }
                dirty_steps = dirty_page = 1;
            } else if ((is_seq_page(page) || page == PG_MIXER) && !layer) {
                uint32_t from = pat_queue ? pat_queue - 1u : set.pat;
                pat_go((from + (e > 0 ? 1u : NPAT - 1u)) % NPAT);
                dirty_hdr = dirty_page = dirty_steps = 1;
            } else {
                set.tempo = (uint8_t)clampi(set.tempo + e, 40, 240); ctl_tempo = set.tempo;
                set_dirty = 1; set_t = fm1_ms;
                msg[0] = 0; scat(msg, "TEMPO "); fmt_u(msg + slen(msg), set.tempo);
                say(msg);
                dirty_page |= page == PG_GLOBAL;
            }
        }
        for (k = 0; k < 4u; k++) {
            int32_t p = PAGE[page].p[k];
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) == 0) continue;
            if (e > 1 || e < -1) e *= 3;        /* a fast turn goes further */
            if (page == PG_MIXER && !layer) {  /* the mixer: track k's LEVEL / PAN / SEND */
                if (mix_row == 6u) {            /* (VIBE) MASTER */
                    if (k == 0) set.m_vol = (uint8_t)clampi((int32_t)set.m_vol + e, 0, 127);
                    else if (k == 1) set.m_dj = (uint8_t)clampi((int32_t)set.m_dj + e, 0, 128);
                    else if (k == 2) set.m_drive = (uint8_t)clampi((int32_t)set.m_drive + e * 2, 0, 127);
                    else set.m_comp = (uint8_t)clampi((int32_t)set.m_comp + e * 2, 0, 127);
                }
                else if (mix_row == 0u) set.mix_lvl[k] = (uint8_t)clampi((int32_t)set.mix_lvl[k] + e, 0, 127);
                else if (mix_row == 1u) set.mix_pan[k] = (int8_t)clampi((int32_t)set.mix_pan[k] + e, -64, 64);
                else { uint8_t *sv = mix_row == 2u ? set.mix_cho : mix_row == 3u ? set.mix_dly : mix_row == 5u ? set.mix_drv : set.mix_rev; sv[k] = (uint8_t)clampi((int32_t)sv[k] + e * 2, 0, 127); }
                mix_apply(); set_dirty = 1; set_t = fm1_ms;
                dirty_steps = 1;
            } else if (page == PG_DRIVE && !layer) {   /* (VIBE) TYPE, DRIVE, TONE, MIX */
                if (k == 0) set.dv_type = (uint8_t)clampi((int32_t)(set.dv_type % NDV) + (e > 0 ? 1 : -1), 0, NDV - 1);
                else { uint8_t *v = k == 1 ? &set.dv_amt : k == 2 ? &set.dv_tone : &set.dv_mix; *v = (uint8_t)clampi((int32_t)*v + e * 2, 0, 127); }
                mix_apply(); set_dirty = 1; set_t = fm1_ms;
            } else if (page == PG_GLOBAL && !layer && !SMK_PAD_HELD) {   /* (a held layer has the knobs, on every page) */
                if (k == 0) { set.tempo = (uint8_t)clampi(set.tempo + e, 40, 240); ctl_tempo = set.tempo; }
                else if (k == 1) set.click = (uint8_t)clampi((int32_t)(set.click % NCLICK) + (e > 0 ? 1 : -1), 0, NCLICK - 1);
                else if (k == 2) set.midi_ch = (uint8_t)clampi(set.midi_ch + e, 0, 16);
                else { set.tune = (int8_t)clampi(set.tune + e, -50, 50); ctl_tune8 = set.tune * 256 / 100; }
                set_dirty = 1; set_t = fm1_ms;
            } else if (is_seq_page(page) && !layer && step_held < 0) {   /* the full-screen sequencer's view */
                seq_view_turn(k, e);
                dirty_steps = 1;
            } else if (layer == L_EDIT) {
                edit_turn(k, e);
                dirty_steps = 1;
            } else if (layer == L_FX) {
                static const int8_t FXP[4] = {P_CHO_MIX, P_DLY_MIX, P_REV_MIX, P_DLY_TIME};
                layer_used = 1;
                set_param((uint32_t)FXP[k], (int32_t)live.v[FXP[k]] + (FXP[k] == P_DLY_TIME ? (e > 0 ? 1 : -1) : e * 2));
                dirty_tags = 1;
            } else if (layer == L_GLO || layer == L_SAVE) {
                ;
            } else if (seq_shown()) {
                layer_used = 1;
                seq_turn(k, e);
                dirty_steps = 1;
            } else if (p >= 0) {
                int32_t step = PDEF[p].max >= 100 ? 2 : 1;
                set_param((uint32_t)p, (int32_t)ed_patch((uint32_t)p)->v[p] + e * step);
                auto_rec((uint32_t)p);
                if (p == P_MODE || p == P_FTYPE || p == P_ARP_MODE || p >= P_CHO_RATE) dirty_tags = 1;
                dirty_hdr = 1;
            }
            dirty_page = 1;
        }

#if BOARD_SMK37
        smk_faders(&dirty_page, &dirty_hdr, &dirty_tags);
        smk_wheels();
#endif
        midi_in(0);
        if (ctl_ext_quarter && fm1_ms - clk_last > 1000u) { ctl_ext_quarter = 0; clk_n = 0; }   /* (the clock stopped: our own tempo) */

        /* LEDs: the keys that sound, the lit buttons; in the step view the white keys are the steps (glowing: a note
         * there, bright: playing now or held) and the black keys their functions (bright: on, glowing: available) */
        glow = 0;
        if (steps_on()) {                       /* the SEQ layer: set steps lit, the playhead blinks them, keys 1 5 9 13 glow */
            uint32_t t = seq_trk, len = pat.trk[t].len, w0 = seq_pg[t] * 16u, ph = len ? ui_seq_pos[t] % len : 0u;
            for (k = 0; k < 27u; k++) {
                if (KEY_STEP[k] >= 0) {
                    uint32_t n = w0 + (uint32_t)KEY_STEP[k], on;
                    const step_t *st;
                    if (n >= len) continue;
                    st = seq_step(t, n);
                    on = st->note[0] && !(st->ratch & STEP_OFF);
                    if (ctl_seq_run && n == ph) on = !on;
                    if (on || (int32_t)n == step_held) lit |= 1u << k;
                    else if (!(KEY_STEP[k] & 3)) glow |= 1u << k;
                } else if (KEY_PAGE[k] >= 0 && (uint32_t)KEY_PAGE[k] < seq_pages(t)) {
                    if ((uint32_t)KEY_PAGE[k] == seq_pg[t]) lit |= 1u << k; else glow |= 1u << k;
                }
            }
        } else if (layer == L_GLO) {            /* 1-4 lit: playing (blinking: on the bar); 5-8 muted; 9-12 solo; G5 the beat */
            static const uint8_t WK[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
            for (k = 0; k < NTRK; k++) {
                uint32_t on = ctl_seq_run && (ctl_trk_on >> k & 1u) && pat.trk[k].len, pend = trk_pending() >> k & 1u;
                if (pend ? (fm1_ms / 150u) & 1u : on) lit |= 1u << WK[k]; else glow |= 1u << WK[k];
                if (set.mix_mute >> k & 1u) lit |= 1u << WK[4u + k]; else glow |= 1u << WK[4u + k];
                if (set.mix_solo >> k & 1u) lit |= 1u << WK[8u + k]; else glow |= 1u << WK[8u + k];
            }
            if (fm1_ms - beat_t < 90u && ctl_seq_run) lit |= 1u << WK[15]; else glow |= 1u << WK[15];
        } else if (layer == L_SAVE) {           /* the pattern now lit, the others glow */
            static const uint8_t WK[8] = {0, 2, 4, 6, 7, 9, 11, 12};
            for (k = 0; k < NPAT; k++) { if (k == set.pat) lit |= 1u << WK[k]; else glow |= 1u << WK[k]; }
            for (k = 0; k < 27u; k++)            /* the black keys: copy targets, glowing where a pattern is saved */
                if (KEY_PAGE[k] >= 0 && pat_used >> KEY_PAGE[k] & 1u) glow |= 1u << k;
            if (pat_queue && (fm1_ms / 200u) & 1u) lit |= 1u << WK[pat_queue - 1u];   /* the next one blinks */
        } else {
            for (k = 0; k < NV; k++)
                if (ui_vlevel[k] > 300 && ui_vnote[k] >= base && ui_vnote[k] < base + 27u)
                    lit |= 1u << (ui_vnote[k] - base);
            if ((rec || entry_active()) && !ctl_seq_run && pat.trk[seq_trk].len) {   /* recording, writing: the keys of the cursor's step */
                const step_t *st = seq_step(seq_trk, seq_cur[seq_trk] % pat.trk[seq_trk].len);
                for (k = 0; k < SEQ_CHORD && st->note[k]; k++)
                    if (st->note[k] >= base && st->note[k] < base + 27u) lit |= 1u << (st->note[k] - base);
            }
        }
        if (ui_seq_beat != beat_seen) { beat_seen = ui_seq_beat; beat_t = fm1_ms; }
        {
            uint32_t play_on = ctl_seq_run && fm1_ms - beat_t < 90u;   /* PLAY flashes on the beat */
            uint32_t rec_on = rec == REC_ON || (rec == REC_READY && (fm1_ms / 250u) & 1u);
            uint32_t pg = steps_on();
            uint32_t on = pg ? seq_pg[seq_trk] > 0u : set.oct < 0, up2 = pg ? seq_pg[seq_trk] + 1u < seq_pages(seq_trk) : set.oct > 0;
            lit |= (play_on << 27) | ((uint32_t)(ed_patch(P_ARP_MODE)->v[P_ARP_MODE] != 0) << 28) | (on << 29) | (up2 << 30) | (rec_on << 31);
        }
        if (lit != leds_prev || glow != glow_prev) {
            leds_prev = lit;
            glow_prev = glow;
            for (k = 0; k < 27u; k++) { fm1_led_key(KEY_LED(k), lit >> k & 1u); fm1_led_key_glow(KEY_LED(k), glow >> k & 1u); }
            fm1_led_key(BTN_PLAY, lit >> 27 & 1u);
            fm1_led_key(BTN_ARP, lit >> 28 & 1u);
            fm1_led_key(BTN_OCT_DOWN, lit >> 29 & 1u);
            fm1_led_key(BTN_OCT_UP, lit >> 30 & 1u);
            fm1_led_key(BTN_REC, lit >> 31 & 1u);
        }
        fm1_led_key(BTN_SEQ, seq_shown());
        fm1_led_key(BTN_HOME, page == PG_PERFORM);
        fm1_led_key(BTN_EDIT, page >= PG_OSC1 && page <= PG_FILTER2);
        fm1_led_key(BTN_ENV, page == PG_FENV || page == PG_AENV);
        fm1_led_key(BTN_LFO, page >= PG_LFO && page <= PG_LFO2);
        fm1_led_key(BTN_FX, page >= PG_CHORUS && page <= PG_DRIVE);
        fm1_led_key(BTN_GLO, page >= PG_OUTPUT && page <= PG_GLOBAL);
        fm1_led_key(BTN_SAVE, save_armed);
#if BOARD_SMK37
        smk_leds();                             /* the step pads, FADER BANK, < > */
#endif

#if BOARD_SMK37
        {   /* KNOB BANK held: the panel check owns the screen; let go, everything is drawn again */
            uint32_t redraw = 0;
            if (smk_check(&redraw)) continue;
            if (redraw) {
                lcd_fill(0, 0, 240, 240, C_PANEL);   /* (the strips leave gaps between them) */
                dirty_hdr = dirty_page = dirty_steps = dirty_tags = 1; hints_key = leds_q = 0xFFFFFFFFu; scope_state = 1; t_scope = 0;
                smk_ov_dirty = 1;
            }
        }
#endif
        {
        /* the screen: only what changed */
        idle_ok = 1;
        if (is_seq_page(page)) {                /* the full-screen sequencer: its own strips */
            static uint32_t top_key, t_seq, info_key;
            static char info_prev[40];
            uint32_t tk = (ctl_ext_quarter ? 60u * FS / ctl_ext_quarter : set.tempo) | rec << 8 | ctl_seq_run << 10 | (uint32_t)set.pat << 11
                        | pat_edited << 14 | trk_silent(seq_trk) << 15 | layer << 16 | SV_EFF << 20 | seq_trk << 23 | (ctl_trk_on ^ trk_pending()) << 26;
            uint32_t ik = (toast && fm1_ms - toast_t < 1500u ? toast_t + 1u : 0u) ^ (arm_k && fm1_ms - arm_t <= 1500u ? 0x80000000u : 0u);
            if (ui_seq_ticks != ticks_seen) { ticks_seen = ui_seq_ticks; dirty_steps = 1; }
            if (dirty_hdr || dirty_tags) { dirty_hdr = dirty_tags = 0; top_key = 0xFFFFFFFFu; }
            if (ik != info_key) { info_key = ik; dirty_page = 1; if (seq_view == SV_SLOTS) dirty_steps = 1; }
            if (fm1_ms - t_seq >= 30u) {
                t_seq = fm1_ms;
                uint32_t ds = dirty_steps, dp = dirty_page;   /* (app_idle may set them again while this draws) */
                dirty_steps = dirty_page = 0;
                if (tk != top_key) { top_key = tk; draw_seq_top(); }
                if (seq_view == SV_SLOTS && !layer) { if (ds) draw_seq_slots(); }
                else if (seq_view == SV_TRACKS && !layer && step_held < 0) {
                    char line[40];
                    uint16_t c;
                    uint32_t n, same = 1;
                    seq_tracks_line(line, &c);
                    for (n = 0; same && (line[n] || info_prev[n]); n++) same = line[n] == info_prev[n];
                    if (ds) draw_seq_tracks();
                    if (ds || !same) { n = 0; while ((info_prev[n] = line[n]) != 0) n++; ui_seq_tline(line, c); }
                } else {
                    char line[40];
                    uint16_t c;
                    uint32_t n, same = 1;
                    seq_info_line(line, &c);
                    for (n = 0; same && (line[n] || info_prev[n]); n++) same = line[n] == info_prev[n];
                    if (ds) { if (seq_view == SV_TRACK && !layer && step_held < 0) draw_seq_trackview(); else draw_seq_roll(); }
                    if (ds || !same) { n = 0; while ((info_prev[n] = line[n]) != 0) n++; draw_seq_info(); }
                }
                if (dp) draw_seq_knobs();
            }
        } else {
        if (dirty_hdr && fm1_ms - t_hdr >= 30u) { dirty_hdr = 0; t_hdr = fm1_ms; draw_header(); }
        if (dirty_page && fm1_ms - t_page >= 30u) { dirty_page = 0; t_page = fm1_ms; draw_page(); }
        if (dirty_tags) { dirty_tags = 0; draw_tags(); }
        if (page == PG_MIXER) {                 /* the mixer: its strip instead of the scope and the voice lights */
            static uint32_t mix_key = 0xFFFFFFFFu;
            uint32_t t, st[NTRK], blink = (fm1_ms / 250u) & 1u, mk = set.mix_mute | set.mix_solo << 4 | seq_trk << 8 | blink << 10;
            static uint8_t meter[NTRK]; static uint32_t t_meter;
            if (fm1_ms - t_meter >= 30u) {      /* (VIBE) the meters: a peak rises at once, falls ~2 px per 30 ms */
                t_meter = fm1_ms;
                for (t = 0; t < NTRK; t++) {
                    uint32_t pk = ui_trk_peak[t], m = 0;
                    ui_trk_peak[t] = 0;
                    while (m < 44u && pk > (uint32_t)(6400u * (m + 1u) / 44u * (m + 1u) / 44u)) m++;   /* (a square law: quiet notes show; 6400 = the output at 0 dBFS) */
                    meter[t] = (uint8_t)(m > meter[t] ? m : meter[t] > 2u ? meter[t] - 2u : 0u);
                }
            }
            for (t = 0; t < NTRK; t++) { st[t] = pat.trk[t].len ? trk_state(t) : 4u; mk = mk * 7u + st[t] + set.mix_lvl[t] * 5u + meter[t] * 131u; }
            if ((dirty_steps || mk != mix_key) && fm1_ms - t_steps >= 30u) {
                dirty_steps = 0; t_steps = fm1_ms; mix_key = mk;
                ui_mix_strip(set.mix_lvl, set.mix_mute, set.mix_solo, st, seq_trk, blink, meter);
            }
        } else {
        if (fm1_ms - t_led >= 40u) {
            uint32_t q = 0;
            t_led = fm1_ms;
            for (k = 0; k < NV; k++) q = q * 9u + ((uint32_t)ui_vlevel[k] >> 12);   /* 8 steps a LED: redraw on a change */
#if BOARD_SMK37
            if (smk_overlay_draw(&leds_q)) ;    /* a fader's bar, a bank, a step page */
            else
#endif
            if (seq_shown() || rec || rec_down || layer) draw_hints();   /* the sequencer's hints instead of the voice LEDs */
            else if (q != leds_q) { leds_q = q; draw_leds(); }
            if (page == PG_GLOBAL && fm1_ms - t_page >= 250u) dirty_page = 1;
        }
        if (seq_shown() || rec) {               /* the sequencer's strip instead of the scope */
            if (ui_seq_ticks != ticks_seen) { ticks_seen = ui_seq_ticks; dirty_steps = 1; }
            if (dirty_steps && fm1_ms - t_steps >= 30u) { dirty_steps = 0; t_steps = fm1_ms; draw_steps(); }
        } else if (scope_state == 3u && fm1_ms - t_scope >= 50u) {
            uint32_t v, best = 0, bl = 0;
            t_scope = fm1_ms;
            draw_scope();
            for (v = 0; v < NV; v++)            /* the stride from the loudest voice's period: ~2 cycles across */
                if ((uint32_t)ui_vlevel[v] > bl) { bl = (uint32_t)ui_vlevel[v]; best = ui_vnote[v]; }
            if (bl) {
                uint32_t inc = pitch_inc((int32_t)best << 8), period = inc ? 0xFFFFFFFFu / inc : 1000u;
                scope_stride = clampi((int32_t)(period * 2u / SCOPE_N), 1, 64);
            }
            scope_state = 1;
        } else if (scope_state == 1u && fm1_ms - t_scope >= 400u) {   /* silence: a flat line */
            t_scope = fm1_ms;
            for (i = 0; i < SCOPE_N; i++) scope[i] = 0;
            draw_scope();
        }
        }
        }
        }
        idle_ok = 0;
        if ((set_dirty || ps_dirty) && !ctl_seq_run && engine_quiet() && fm1_ms - set_t > 2000u) {   /* when silent: an erase stalls the sound */
            uint32_t i;
            for (i = 0; i < NPSECT; i++)        /* the programs before the settings (a 1.4 update: they leave the settings) */
                if (ps_dirty >> i & 1u) psect_save(i, &psect[i], sizeof psect[i]);
            ps_dirty = 0;
            if (set_dirty) settings_save(&set, sizeof set);
            set_dirty = 0;
        }
        /* the patterns: saved by themselves (3 s after an edit, once stopped), as asked, or left while playing;
         * only when stopped and quiet (an erase stalls the sound) */
        if (pat_edited && !pat_dirty) {
            if (!pat_edit_t) pat_edit_t = fm1_ms | 1u;
            else if (fm1_ms - pat_edit_t > 3000u && !ctl_seq_run && !rec && step_held < 0 && !entry_n && !layer) pat_dirty = 1;
        }
        if (ui_pat_swapped != swap_seen) {      /* the ISR swapped the next pattern in on the downbeat, its sounds and mix too */
            static char msg[16];
            uint32_t left = set.pat, t;
            swap_seen = ui_pat_swapped;
            RING_TAKE();
            {   /* the one left keeps its instruments as they sounded to the end, and its mixer */
                psnd_t *ps = &psnd[left];
                ps->magic = PSN2_MAGIC;
                for (t = 0; t < NTRK; t++) {
                    if (ui_snd_swapped) ps->p[t] = snd_old[t];
                    ps->prog[t] = (uint8_t)trk_prog[t]; ps->edited[t] = (uint8_t)trk_edited[t];
                }
                mx_from_set(&ps->mx);
                ui_snd_swapped = 0;
            }
            if (pat_edited && ctl_pat_keep) pend_slot = left + 1u;   /* (its edits in pat_old, saved at the stop) */
            set.pat = (uint8_t)(pat_queue ? pat_queue - 1u : set.pat);
            snd_meta(set.pat);
            pat_queue = 0;
            memset(seq_cur, 0, sizeof seq_cur); memset(seq_pg, 0, sizeof seq_pg);
            pat_edited = q_edited; pat_edit_t = 0; slot_sel = set.pat; q_edited = 0;
            set_dirty = 1; set_t = fm1_ms;
            msg[0] = 0; scat(msg, "PATTERN "); fmt_u(msg + slen(msg), set.pat + 1u);
            say(msg);
            dirty_page = dirty_steps = dirty_hdr = dirty_tags = 1;
        }
        if (pat_queue && !ctl_seq_run) {        /* stopped before the bar: now */
            uint32_t n = pat_queue - 1u;
            ctl_pat_swap = 0; ctl_snd_swap = 0; ctl_mx_swap = 0; pat_queue = 0;
            pat_switch(n);
            dirty_page = dirty_steps = dirty_hdr = 1;
        }
        if (!pat_queue && fm1_ms - snd_t > 1500u) {   /* the sounds now changed (a program, a knob, the mix): this pattern's set follows */
            snd_t = fm1_ms;
            if (snd_changed(set.pat)) snd_capture(set.pat);
        }
        if ((pat_dirty || pend_slot || copy_to || snd_dirty) && !ctl_seq_run && engine_quiet()
            && (pat_dirty || pend_slot || copy_to || (fm1_ms - snd_chg_t > 3000u && step_held < 0 && !entry_n && !layer))) {   /* (an erase stalls the sound: stopped and quiet) */
            static char msg[20];
            uint32_t k;
            if (pend_slot) {                    /* the pattern left while playing, with its edits */
                uint32_t n = pend_slot - 1u;
                if (!slot_save(n, &pat_old, sizeof pat_old, &psnd[n], sizeof psnd[n])) slot_saved(n, &pat_old);
                pend_slot = 0;
            }
            if (pat_dirty) {
                int bad;
                if (snd_changed(set.pat)) snd_capture(set.pat);
                bad = slot_save(set.pat, &pat, sizeof pat, &psnd[set.pat], sizeof psnd[set.pat]);
                if (!bad) slot_saved(set.pat, &pat);
                if (pat_save_say) {             /* (by itself, or before a load: no message) */
                    msg[0] = 0; scat(msg, bad ? "SAVE FAILED " : "SAVED IN "); fmt_u(msg + slen(msg), set.pat + 1u); say(msg);
                }
                pat_dirty = pat_edited = pat_save_say = 0;
                pat_edit_t = 0;
            }
            if (copy_to) {                      /* this pattern with its sounds into another slot */
                uint32_t n = copy_to - 1u;
                int bad;
                if (snd_changed(set.pat)) snd_capture(set.pat);
                psnd[n] = psnd[set.pat];
                bad = slot_save(n, &pat, sizeof pat, &psnd[n], sizeof psnd[n]);
                if (!bad) slot_saved(n, &pat);
                msg[0] = 0; scat(msg, bad ? "COPY FAILED " : "COPIED TO "); fmt_u(msg + slen(msg), copy_to); say(msg);
                copy_to = 0;
            }
            for (k = 0; k < NPAT && snd_dirty; k++) {   /* a pattern whose sounds alone changed: its slot again */
                if (!(snd_dirty >> k & 1u)) continue;
                if (!(pat_used >> k & 1u) && !(k == set.pat && pat_edited)) { snd_dirty &= ~(1u << k); continue; }   /* (an empty one: in RAM) */
                if (k == set.pat) {
                    if (!slot_save(k, &pat, sizeof pat, &psnd[k], sizeof psnd[k])) { slot_saved(k, &pat); pat_edited = pat_dirty = 0; pat_edit_t = 0; }
                } else {
                    pat_fetch(k, &pat_q);       /* (stopped: pat_q is free) */
                    if (!slot_save(k, &pat_q, sizeof pat_q, &psnd[k], sizeof psnd[k])) slot_saved(k, &pat_q);
                }
                snd_dirty &= ~(1u << k);
            }
            dirty_page = dirty_steps = dirty_hdr = 1;
        }
        if (pat_next && !pat_dirty) {           /* a load that waited for the save */
            uint32_t n = pat_next - 1u;
            pat_next = 0;
            pat_switch(n);
            dirty_page = dirty_steps = dirty_hdr = 1;
        }
    }
}
