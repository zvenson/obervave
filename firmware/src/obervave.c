/* SPDX-License-Identifier: GPL-3.0-only */
/* DØG-5 (forked from OberVave): one 8-voice analog-style polysynth for the M-VAVE FM-1. Built on Dinghy's
 * platform (platform.c: boot, USB, updates, audio ISR, settings).
 *
 *   obv_dsp.h       oscillators (polyBLEP events), filter (trapezoidal SVF), tanh
 *   obv_params.h    the parameter table: patches, pages, MIDI CC
 *   obv_fx.h        chorus, ping-pong delay, plate reverb (after the voices, same ISR)
 *   obv_engine.h    the voices, envelopes, LFO, arpeggiator: all in the audio ISR
 *   obv_patches.h   the factory programs
 *   this file       the panel, MIDI, the screen, the storage
 *
 * (OberVave's step sequencer, obv_seq.h, and the two-sound track B it alone drove, are gone: CLAUDE.md §5.
 * SEQ, ALGORITHM, PLAY and REC are free for a later DØG-5 stage to claim.)
 *
 * Integers only (no FPU, no 64-bit division); tests/host.c runs this on a computer. */
static void app_idle(void);
#define LCD_IDLE() app_idle()                   /* (lcd.c: while the DMA runs) */
#include "platform.c"
#include "obv_dsp.h"
#include "obv_params.h"
#include "obv_fx.h"
#include "obv_engine.h"
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
#define SET_VERSION 3u                          /* 3: the programs in their own sectors (99); 2: 32 in here, 80 bytes */
static struct {
    uint8_t version, prog, tempo, midi_ch;      /* midi_ch 0 = omni, else 1..16 */
    int8_t tune, oct;
    uint8_t pat, click;                         /* unused: freed with the sequencer (DØG-5 deleted it); kept so the
                                                  * settings sector's layout, and SET_VERSION, don't have to change */
} set;
#define MIDI_OUT_CH (set.midi_ch ? set.midi_ch - 1u : 0u)   /* the keys go out on the MIDI channel set (OMNI: 1) */
/* (the OberVave SET_VERSION 2 migration that brought 32 old programs over embedded an 80-byte patch_t; with the
 * 96-byte DØG-5 record its layout no longer matches, and a DØG-5 FM-1 arrives empty, so it is dropped -- old
 * settings/program sectors read as empty, never misread.) */
static uint32_t set_dirty, set_t;
/* the user's programs: PS_N to a flash sector (psect_load / psect_save), all of them in RAM */
#define PS_N 42u                                /* 96-byte patches: 42 per 4 KiB sector (42*96=4032; psect_t=4080 <= SETTINGS_MAX=4084) */
#define PS_TAG 0x35474F44u                      /* "DOG5": the 96-byte program format. Old "BOVP"/80-byte sectors fail
                                                 * sector_load's length check (and this tag check) and read as empty. */
typedef struct { uint32_t tag; uint8_t used[PS_N], pad[2]; patch_t p[PS_N]; } psect_t;
_Static_assert(sizeof(psect_t) <= SETTINGS_MAX, "a program sector outgrew its flash sector");
_Static_assert(NPSECT * PS_N >= NPROG, "too few program sectors");
static psect_t psect[NPSECT];
static uint32_t ps_dirty;                       /* bit i: program sector i to save */

static uint32_t prog, edited, page = PG_PERFORM, hold, save_armed, save_t, save_to;   /* save_to: SAVE's slot (PRESETS) */   /* hold: the sound's HOLD */
static uint32_t toast_t;
static const char *toast;
/* the panel as sloopDX's: a function button tapped (let go, nothing else touched) opens its pages; FX held is a
 * layer (its knobs change job until it is let go; HOME tapped while it is held locks it open). SEQ / ALGORITHM /
 * PLAY / REC are free (DØG-5 deleted the sequencer and the two-sound track B it alone drove: obv_seq.h,
 * CLAUDE.md §5) -- a later DØG-5 stage claims them. */
enum { L_NONE, L_FX };
static uint32_t layer, layer_lock, layer_used, layer_t, layer_btn;
#define SMK_PAD_HELD 0                           /* (the step pads are freed with the sequencer, both boards) */
#if BOARD_SMK37
#define KEY_VEL(k) fm1_in.key_vel[SMK_KEY_F3 + (k)]   /* the keys play with velocity (the unit's factory curves) */
#define NM_PRESETS "K4"                         /* the SMK's printed knob name */
#else
#define KEY_VEL(k) 100
#define NM_PRESETS "PRESETS"
#endif
static void say(const char *s) { toast = s; toast_t = fm1_ms; }
static uint8_t cc_map[128];                     /* MIDI CC -> parameter + 1, 0 = none */

static void prog_get(uint32_t n, patch_t *p)    /* program n: the user's, else the factory's (65-99: INIT) */
{
    n %= NPROG;
    if (psect[n / PS_N].used[n % PS_N]) *p = psect[n / PS_N].p[n % PS_N];
    else patch_from_factory(p, n < NFACT ? &FACTORY[n] : &FACT_INIT);
}

static void prog_put(uint32_t n, const patch_t *p)   /* program n is the user's now; saved when quiet */
{
    n %= NPROG;
    psect[n / PS_N].p[n % PS_N] = *p;
    psect[n / PS_N].used[n % PS_N] = 1;
    ps_dirty |= 1u << (n / PS_N);
    set_t = fm1_ms;
}

static void load_program(uint32_t n)
{
    patch_t p;
    n %= NPROG;
    prog_get(n, &p);
    live = p;
    prog = n;
    edited = 0;
}

static void play_note(uint32_t on, uint32_t note, uint32_t vel) { note_event(on, note & 127u, vel); }

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

static void set_param(uint32_t p, int32_t v)
{
    v = clampi(v, 0, PDEF[p].max);
    if (live.v[p] == v) return;
    live.v[p] = (uint8_t)v;
    edited = 1;
}

/* REC: a new sound within useful ranges */
static uint32_t rnd;
static uint32_t rn(uint32_t lo, uint32_t hi) { return lo + xorshift(&rnd) % (hi - lo + 1u); }
static void randomize(void)
{
    uint32_t i, r;
    patch_t *d = &live;
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
    edited = 1;
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
        ui_header(save_to, t.v + PATCH_NAME_AT, 0, tags);
        return;
    }
    if (cut_shown_t && fm1_ms + 1u - cut_shown_t < 1000u) {
        char v[12];
        param_value(v, P_CUTOFF, live.v[P_CUTOFF]);
        scat(tags, "CUTOFF ");
        scat(tags, v);
        ui_header(prog, live.v + PATCH_NAME_AT, edited, tags);
        return;
    }
    {   /* short words; mode, filter, HOLD always (at most 14 characters: MONO NOTCH HOLD); then, as far as it fits, ARP, FX */
        static const char *const MODE_S[4] = {"POLY", "DUAL", "UNI", "MONO"};
        static const char *const FT_S[NFTYPE] = {"LP12", "HP12", "BP12", "NOTCH", "LP24", "SEM"};
        const uint8_t *pv = live.v;
        uint32_t arp = pv[P_ARP_MODE] != 0, fx = live.v[P_CHO_MIX] || live.v[P_DLY_MIX] || live.v[P_REV_MIX];
        uint32_t n = slen(MODE_S[pv[P_MODE] & 3u]) + 1u + slen(FT_S[pv[P_FTYPE] % NFTYPE]) + (hold ? 5u : 0u);
        if (arp && n + 4u <= 23u) n += 4u; else arp = 0;
        if (fx && n + 3u <= 23u) n += 3u; else fx = 0;
        scat(tags, MODE_S[pv[P_MODE] & 3u]);
        scat(tags, " ");
        scat(tags, FT_S[pv[P_FTYPE] % NFTYPE]);
        if (arp) scat(tags, " ARP");
        if (fx) scat(tags, " FX");
        if (hold) scat(tags, " HOLD");
    }
    ui_header(prog, live.v + PATCH_NAME_AT, edited, tags);
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

/* FX held: its knobs are the effects' mixes (the only layer left: SEQ/EDIT/GLO/SAVE's hold-layers were sequencer
 * content and went with it; EDIT, GLO and SAVE are plain tap buttons now, see app_main) */
static void layer_knob(knob_view_t *k, uint32_t i)
{
    static const int8_t FXP[4] = {P_CHO_MIX, P_DLY_MIX, P_REV_MIX, P_DLY_TIME};
    uint32_t p2 = (uint32_t)FXP[i];
    k->present = 1; k->label = PDEF[p2].name; k->v = live.v[p2]; k->max = PDEF[p2].max;
    param_value(k->value, p2, live.v[p2]);
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
        if (page == PG_GLOBAL && !layer && !SMK_PAD_HELD && i != 1u) {
            static const char *const GL[4] = {"TEMPO", "", "MIDI CH", "TUNE"};
            k[i].label = GL[i];
            k[i].present = 1;
            if (i == 0) { fmt_u(k[i].value, set.tempo); k[i].v = set.tempo - 40; k[i].max = 200; }
            else if (i == 2) { if (set.midi_ch) fmt_u(k[i].value, set.midi_ch); else scat(k[i].value, "OMNI"); k[i].v = set.midi_ch; k[i].max = 16; }
            else { scat(k[i].value, set.tune < 0 ? "-" : "+"); fmt_u(k[i].value + 1, (uint32_t)(set.tune < 0 ? -set.tune : set.tune)); scat(k[i].value, "c"); k[i].v = set.tune + 50; k[i].max = 100; k[i].bip = 1; }
        } else if (layer == L_FX) {
            layer_knob(&k[i], i);
        } else if (p >= 0) {
            k[i].label = PDEF[p].name;
            k[i].v = live.v[p];
            k[i].max = PDEF[p].max;
            k[i].bip = PDEF[p].kind == K_BIP || PDEF[p].kind == K_SEMI || PDEF[p].kind == K_MORPH;
            k[i].present = 1;
            param_value(k[i].value, (uint32_t)p, (uint32_t)live.v[p]);
        }
    }
    {
        const char *t = pg->title;
        if (layer == L_FX) t = "FX MIX";
        {
            static char cpu[16];                /* the GLOBAL page: the render time of an audio half (budget 5800 us) */
            cpu[0] = 0; scat(cpu, "CPU "); fmt_u(cpu + slen(cpu), ui_render_us); scat(cpu, "us");
            ui_page(t, k, save_armed ? "SAVE?" : page == PG_GLOBAL && !layer ? cpu : 0);
        }
    }
}

/* the hint line under the scope: a toast after an action, else FX held's knob hint (the only layer left) */
static uint32_t hints_key = 0xFFFFFFFFu;
static void draw_hints(void)
{
    uint32_t key;
    int toasting = toast && fm1_ms - toast_t < 1500u;
    key = toasting ? 100u + ((toast_t / 7u) & 0xFFu) : 400u + layer;
    if (key == hints_key) return;
    hints_key = key;
    if (toasting) ui_hints(toast, "", C_AMBER);
    else if (layer == L_FX) ui_hints("KNOBS: CHORUS DELAY REVERB", "AND THE DELAY'S TIME", C_AMBER);
}

static void draw_tags(void) { draw_header(); }
static void draw_scope(void) { ui_scope(scope, SCOPE_N); }
static void draw_leds(void) { ui_leds(ui_vlevel, NV); }

/* ---- the main loop */
#define KEY0 53u                               /* key 0 = F3 */
/* the keys and MIDI in: read by the main loop, and while the screen is drawn (app_idle, notes only), so the notes
 * of a chord or among a stream of CCs are not held up by a redraw (a knob page is ~30 ms on the SPI alone) */
static uint32_t dirty_hdr = 1, dirty_page = 1, dirty_tags = 1;
static uint32_t keys_prev, clk_n, clk_t0, clk_last, idle_ok;
static uint8_t held_key[27];

#if BOARD_SMK37
#include "obv_smk.h"                            /* the SMK-37's outer keys, faders, wheels, < >, FADER BANK */
#endif

static uint32_t cycle_page(uint32_t first, uint32_t count)   /* a button steps through its pages */
{
    if (page >= first && page < first + count) return first + (page - first + 1u) % count;
    return first;
}

static void keys_in(void)
{
    uint32_t keys = fm1_in.notes, down = keys & ~keys_prev, up = keys_prev & ~keys, base = (uint32_t)(KEY0 + 12 * set.oct), k;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u) {
            keys_prev |= 1u << k;
            held_key[k] = (uint8_t)(base + k);
            play_note(1, held_key[k], KEY_VEL(k)); midi_send(0x90u | MIDI_OUT_CH, held_key[k], KEY_VEL(k));
        }
        if (up >> k & 1u) {
            keys_prev &= ~(1u << k);
            play_note(0, held_key[k], 0); midi_send(0x80u | MIDI_OUT_CH, held_key[k], 0);
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
        if (st == 0xF8u) {                  /* clock: 24 per quarter; the tempo follows, 2 s without it and it is gone */
            if (!clk_n) clk_t0 = fm1_ms;
            clk_last = fm1_ms;
            if (++clk_n == 25u) {
                uint32_t ms = fm1_ms - clk_t0;
                if (ms > 200u && ms < 1600u) ctl_ext_quarter = ms * 441u / 10u;
                clk_n = 0;
            }
            continue;
        }
        if (st == 0xFAu || st == 0xFBu) {   /* start / continue: the arp from the top */
            ctl_arp_restart = 1; clk_n = 0;
            continue;
        }
        if (st >= 0xF0u) continue;
        if (set.midi_ch && ch != set.midi_ch) continue;
        if (kind == 0x90u && d2) play_note(1, d1, d2);
        else if (kind == 0x80u || kind == 0x90u) play_note(0, d1, 0);
        else if (kind == 0xE0u) ctl_bend = (int32_t)(d1 | d2 << 7) - 8192;
        else if (kind == 0xC0u && d1 < NPROG) { load_program(d1); dirty_hdr = dirty_page = dirty_tags = 1; set.prog = (uint8_t)prog; }
        else if (kind == 0xB0u) {
            if (d1 == 7u) ctl_cc7 = d2;
            else if (d1 == 64u) ctl_sustain = d2 >= 64u;
            else if (d1 == 120u || d1 == 123u) ctl_panic = 1;
            else if (cc_map[d1]) {
                uint32_t p = cc_map[d1] - 1u;
                set_param(p, PDEF[p].max == 127 ? (int32_t)d2 : ((int32_t)d2 * PDEF[p].max + 63) / 127);
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
    keys_in();
    midi_in(1);
    busy = 0;
}

#ifdef OBV_HOST
/* a host self-test of the 96-byte / 3-sector / DOG5 program store, exercising the REAL app code (prog_put,
 * prog_get, psect_save, psect_load, the boot reload) over tests/host.c's flash model. Run: `obervave selftest`.
 * (The device's own sector_load/save -- magic + length + CRC32 at real flash addresses -- run only on the
 * toolchain/device; here the host model's length guard stands in for the format/version check.) */
static int sf_fail;
static void sf_check(const char *what, int ok) { printf("selftest: %-64s %s\n", what, ok ? "ok" : "FAIL"); sf_fail += !ok; }
static void sf_seed(patch_t *p, uint32_t seed)   /* a distinct, fully-filled 96-byte patch (all bytes vary by seed) */
{
    uint32_t i, s = seed * 2654435761u + 1u;
    for (i = 0; i < PATCH_BYTES; i++) { s = s * 1103515245u + 12345u; p->v[i] = (uint8_t)(s >> 24); }
}
static patch_t sf_expect[NPROG];
static uint8_t sf_snap_ps[NPSECT][sizeof host_psect[0]], sf_snap_set[sizeof host_store], sf_snap_bl[NBLOB][sizeof host_blob[0]];
static void store_selftest(void)
{
    uint32_t n, s;
    patch_t got;
    printf("selftest: PATCH_BYTES=%u NPARAM=%u name@%u  PS_N=%u NPSECT=%u sizeof(psect_t)=%zu PS_TAG=%08X\n",
           PATCH_BYTES, NPARAM, PATCH_NAME_AT, PS_N, NPSECT, sizeof(psect_t), PS_TAG);

    /* fresh flash + fresh RAM caches */
    memset(host_psect, 0, sizeof host_psect);
    for (n = 0; n < NPSECT; n++) { memset(&psect[n], 0, sizeof psect[n]); psect[n].tag = PS_TAG; }
    ps_dirty = 0;

    /* ---- c: with no user saves, programs 1-32 are the DØG-5 factory, 33-99 all read INIT ---- */
    {
        int c_ok = 1;
        for (n = 32; n < NPROG; n++) {           /* 0-idx 32-98 == programs 33-99 */
            prog_get(n, &got);
            if (memcmp(got.v + PATCH_NAME_AT, "INIT", 4) != 0 || got.v[PATCH_NAME_AT + 4] != ' ') c_ok = 0;
        }
        sf_check("with no user saves, programs 33-99 all read INIT", c_ok);
    }

    /* ---- a1: 99 distinct patches into all 99 slots, power-cycle, reload, compare every byte ---- */
    for (n = 0; n < NPROG; n++) { sf_seed(&sf_expect[n], n + 1u); live = sf_expect[n]; prog_put(n, &live); }
    for (s = 0; s < NPSECT; s++) if (ps_dirty >> s & 1u) psect_save(s, &psect[s], sizeof psect[s]);
    ps_dirty = 0;
    sf_check("all 3 program sectors were written (99 slots span 3 sectors)",
             host_slen(host_psect[0]) == sizeof(psect_t) && host_slen(host_psect[1]) == sizeof(psect_t) &&
             host_slen(host_psect[2]) == sizeof(psect_t));
    memset(psect, 0, sizeof psect);                  /* power cycle: the RAM caches are lost, the flash survives */
    for (n = 0; n < NPSECT; n++)
        if (psect_load(n, &psect[n], sizeof psect[n]) || psect[n].tag != PS_TAG) { memset(&psect[n], 0, sizeof psect[n]); psect[n].tag = PS_TAG; }
    {
        int all = 1, s3 = 1;
        for (n = 0; n < NPROG; n++) { prog_get(n, &got); if (memcmp(&got, &sf_expect[n], PATCH_BYTES)) { all = 0; printf("  slot %u differs\n", n + 1u); } }
        for (n = 84; n < NPROG; n++) if (n / PS_N != 2u) s3 = 0;   /* programs 85-99 (0-idx 84-98) */
        sf_check("all 99 patches survive the power-cycle byte-exact (every byte of every patch)", all);
        sf_check("programs 85-99 live in the 3rd sector (index 2)", s3);
    }

    /* ---- a2: saving one slot touches only its sector; the other two + all other flash unchanged ---- */
    memcpy(sf_snap_ps, host_psect, sizeof host_psect);
    memcpy(sf_snap_set, host_store, sizeof host_store);
    memcpy(sf_snap_bl, host_blob, sizeof host_blob);
    sf_seed(&live, 7777u); prog_put(50u, &live);     /* program 51 (0-idx 50) -> sector 50/42 = 1 */
    for (s = 0; s < NPSECT; s++) if (ps_dirty >> s & 1u) psect_save(s, &psect[s], sizeof psect[s]);
    ps_dirty = 0;
    sf_check("saving program 51 rewrote only its sector (1); sectors 0 and 2 unchanged",
             memcmp(host_psect[1], sf_snap_ps[1], sizeof host_psect[1]) != 0 &&
             memcmp(host_psect[0], sf_snap_ps[0], sizeof host_psect[0]) == 0 &&
             memcmp(host_psect[2], sf_snap_ps[2], sizeof host_psect[2]) == 0);
    sf_check("that save left the settings sector and all 8 pattern sectors untouched",
             memcmp(host_store, sf_snap_set, sizeof host_store) == 0 && memcmp(host_blob, sf_snap_bl, sizeof host_blob) == 0);

    /* ---- b: an old 80-byte-format sector (tag "BOVP", 4056 bytes) reads as empty, is never misread ---- */
    {
        static uint8_t old[4056];                    /* the old psect_t size: 4 tag + 50 used + 2 pad + 50*80 */
        uint32_t oldtag = 0x50564F42u;               /* psect_t.tag == "BOVP" at offset 0 */
        int rej, empty = 1;
        memset(old, 0xAB, sizeof old);
        memcpy(old, &oldtag, 4);
        host_sput(host_psect[0], old, sizeof old);   /* sector 0 now holds the old format (stored length 4056) */
        memset(host_psect[1], 0, 4); memset(host_psect[2], 0, 4);   /* sectors 1,2 empty */
        rej = (psect_load(0, &psect[0], sizeof psect[0]) != 0);     /* must reject: stored 4056 != requested 4080 */
        for (n = 0; n < NPSECT; n++)
            if (psect_load(n, &psect[n], sizeof psect[n]) || psect[n].tag != PS_TAG) { memset(&psect[n], 0, sizeof psect[n]); psect[n].tag = PS_TAG; }
        for (n = 0; n < NPROG; n++) if (psect[n / PS_N].used[n % PS_N]) empty = 0;
        sf_check("the old 80-byte \"BOVP\" sector (4056 B) is rejected by the length guard", rej);
        sf_check("after rejection every slot reads empty (factory/INIT); the DOG5 tag is set, no BOVP/junk leaked",
                 empty && psect[0].tag == PS_TAG);
    }

    printf("selftest: %s\n", sf_fail ? "FAILED" : "all ok");
    exit(sf_fail ? 1 : 0);
}
#endif

static void app_main(void)
{
    uint32_t k, i, t_led = 0, t_scope = 0, t_page = 0, t_hdr = 0, leds_q = 1;
    uint32_t leds_prev = 0xFFFFFFFFu, glow = 0, glow_prev = 0xFFFFFFFFu;
#ifdef OBV_HOST
    if (host_selftest) store_selftest();             /* runs the store self-test and exit()s */
#endif
    rnd = 0xC0FFEE42u;
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
            memset(&set, 0, sizeof set);
            set.version = SET_VERSION;
            set.tempo = 120;
        }
    }
    if (set.prog >= NPROG) set.prog = 0;
    if (set.tempo < 40u || set.tempo > 240u) set.tempo = 120;
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.midi_ch > 16u) set.midi_ch = 0;
    ctl_tempo = set.tempo;
    ctl_tune8 = set.tune * 256 / 100;
    load_program(set.prog);
    ctl_panic = 0;

    ui_splash();
    scope_state = 1;

    for (;;) {
        uint32_t rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct), np = page;
        int32_t e;
        platform_poll();

        keys_in();
#if BOARD_SMK37
        smk_extkeys(base);                      /* the outer keys */
#endif

        /* buttons, as sloopDX: FX held is a layer (its knobs change job until it is let go), HOME while held
         * locks it open; EDIT, GLO and SAVE are plain taps now (their hold-layers were sequencer content: erase
         * notes, mute/solo/tap-tempo, pattern slots -- all gone with it, see the enum above) */
        b = fm1_input_edges(&rel);
        {
            static uint32_t dead;               /* a press that only unlocked FX: its release does nothing */
            if (b & (1u << BTN_FX)) {
                if (layer_lock) { layer = L_NONE; layer_lock = 0; dead = 1; }
                else { layer = L_FX; layer_used = 0; layer_t = fm1_ms; layer_btn = BTN_FX; }
                dirty_page = dirty_tags = 1; hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
            }
            if (rel & (1u << BTN_FX)) {
                if (dead) dead = 0;
                else if (layer == L_FX && !layer_lock) {
                    if (!layer_used && fm1_ms - layer_t < 600u) np = cycle_page(PG_CHORUS, 3);
                    layer = L_NONE;
                    dirty_page = dirty_tags = 1; hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
                }
            }
        }
#if BOARD_SMK37
        smk_buttons(b);                         /* FADER BANK, < > */
#endif
        if (b & (3u << BTN_OCT_DOWN)) {
            if ((fm1_in.buttons & (3u << BTN_OCT_DOWN)) == (3u << BTN_OCT_DOWN)) {   /* both: octave 0 */
                set.oct = 0; set_dirty = 1; set_t = fm1_ms; say("OCTAVE 0");
            } else {
                if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; set_dirty = 1; set_t = fm1_ms; }
                if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; set_dirty = 1; set_t = fm1_ms; }
            }
            dirty_page = 1;
        }
        if (b & (1u << BTN_HOME)) {             /* HOME: PERFORM; while a layer is held: lock it open; locked: let it go */
            if (layer && !layer_lock) { layer_lock = 1; layer_used = 1; say("LOCKED: ITS BUTTON ENDS IT"); }
            else if (layer_lock) { layer = L_NONE; layer_lock = 0; dirty_page = dirty_tags = 1; leds_q = 0xFFFFFFFFu; }
            else np = PG_PERFORM;
        }
        if (b & ((1u << BTN_ENV) | (1u << BTN_LFO) | (1u << BTN_ARP)) && layer_lock) {   /* (another button lets a lock go) */
            layer = L_NONE; layer_lock = 0; b &= ~((1u << BTN_ENV) | (1u << BTN_LFO) | (1u << BTN_ARP));
            dirty_page = dirty_tags = 1; leds_q = 0xFFFFFFFFu;
        }
        if (b & (1u << BTN_EDIT)) np = cycle_page(PG_OSC1, 5);
        if (b & (1u << BTN_ENV)) np = cycle_page(PG_FENV, 2);
        if (b & (1u << BTN_LFO)) np = cycle_page(PG_LFO, 3);
        if (b & (1u << BTN_ARP)) {
            if (page == PG_ARP) { hold ^= 1u; ctl_hold = hold; dirty_tags = 1; }   /* on its page: HOLD */
            else np = PG_ARP;
        }
        if (b & (1u << BTN_GLO)) np = page == PG_GLOBAL ? PG_VOICE : page == PG_VOICE ? PG_OUTPUT : PG_GLOBAL;
        if (b & (1u << BTN_SAVE)) {              /* a tap: armed (the slot to save into); tapped again: saved */
            if (save_armed) {
                uint32_t n = save_to % NPROG;
                prog_put(n, &live);
                prog = n; set.prog = (uint8_t)n; edited = 0; set_dirty = 1;
                save_armed = 0;
                say("SAVED");
                dirty_hdr = dirty_tags = dirty_page = 1;
            } else { save_armed = 1; save_t = fm1_ms; save_to = prog; dirty_hdr = dirty_page = 1; }
        }
        if (b & (1u << BTN_SEL) && save_armed) {   /* SAVE armed + SELECT pressed: a new sound (dice) instead */
            randomize();
            save_armed = 0;
            dirty_hdr = dirty_tags = dirty_page = 1;
        }
        if (save_armed && fm1_ms - save_t > 3000u) { save_armed = 0; dirty_page = dirty_hdr = 1; }
        if (np != page) {
            if (layer_lock) { layer = L_NONE; layer_lock = 0; }
            page = np; dirty_page = 1; hints_key = 0xFFFFFFFFu; leds_q = 0xFFFFFFFFu;
        }

        /* encoders */
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            if (save_armed) {                   /* SAVE armed: PRESETS picks the slot to save into ("save as") */
                save_to = (uint32_t)((int32_t)save_to + e + (int32_t)NPROG * 4) % NPROG;
                save_t = fm1_ms;
                dirty_hdr = 1;
            } else {
                load_program((uint32_t)((int32_t)prog + e + (int32_t)NPROG * 4) % NPROG);
                set.prog = (uint8_t)prog; set_dirty = 1; set_t = fm1_ms;
                dirty_hdr = dirty_page = dirty_tags = 1;
            }
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {   /* SELECT: the tempo, always */
            static char msg[16];
            set.tempo = (uint8_t)clampi(set.tempo + e, 40, 240); ctl_tempo = set.tempo;
            set_dirty = 1; set_t = fm1_ms;
            msg[0] = 0; scat(msg, "TEMPO "); fmt_u(msg + slen(msg), set.tempo);
            say(msg);
            dirty_page |= page == PG_GLOBAL;
        }
        for (k = 0; k < 4u; k++) {
            int32_t p = PAGE[page].p[k];
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) == 0) continue;
            if (e > 1 || e < -1) e *= 3;        /* a fast turn goes further */
            if (page == PG_GLOBAL && !layer && !SMK_PAD_HELD && k != 1u) {   /* (a held layer has the knobs, on every page) */
                if (k == 0) { set.tempo = (uint8_t)clampi(set.tempo + e, 40, 240); ctl_tempo = set.tempo; }
                else if (k == 2) set.midi_ch = (uint8_t)clampi(set.midi_ch + e, 0, 16);
                else { set.tune = (int8_t)clampi(set.tune + e, -50, 50); ctl_tune8 = set.tune * 256 / 100; }
                set_dirty = 1; set_t = fm1_ms;
            } else if (layer == L_FX) {
                static const int8_t FXP[4] = {P_CHO_MIX, P_DLY_MIX, P_REV_MIX, P_DLY_TIME};
                layer_used = 1;
                set_param((uint32_t)FXP[k], (int32_t)live.v[FXP[k]] + (FXP[k] == P_DLY_TIME ? (e > 0 ? 1 : -1) : e * 2));
                dirty_tags = 1;
            } else if (p >= 0) {
                int32_t step = PDEF[p].max >= 100 ? 2 : 1;
                set_param((uint32_t)p, (int32_t)live.v[p] + e * step);
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
        if (ctl_ext_quarter && fm1_ms - clk_last > 2000u) { ctl_ext_quarter = 0; clk_n = 0; }

        /* LEDs: the keys that sound, the lit buttons */
        glow = 0;
        for (k = 0; k < NV; k++)
            if (ui_vlevel[k] > 300 && ui_vnote[k] >= base && ui_vnote[k] < base + 27u)
                lit |= 1u << (ui_vnote[k] - base);
        {
            uint32_t on = set.oct < 0, up2 = set.oct > 0;
            lit |= ((uint32_t)(live.v[P_ARP_MODE] != 0) << 28) | (on << 29) | (up2 << 30);
        }
        if (lit != leds_prev || glow != glow_prev) {
            leds_prev = lit;
            glow_prev = glow;
            for (k = 0; k < 27u; k++) { fm1_led_key(KEY_LED(k), lit >> k & 1u); fm1_led_key_glow(KEY_LED(k), glow >> k & 1u); }
            fm1_led_key(BTN_ARP, lit >> 28 & 1u);
            fm1_led_key(BTN_OCT_DOWN, lit >> 29 & 1u);
            fm1_led_key(BTN_OCT_UP, lit >> 30 & 1u);
        }
        fm1_led_key(BTN_HOME, page == PG_PERFORM);
        fm1_led_key(BTN_EDIT, page >= PG_OSC1 && page <= PG_FILTER2);
        fm1_led_key(BTN_ENV, page == PG_FENV || page == PG_AENV);
        fm1_led_key(BTN_LFO, page >= PG_LFO && page <= PG_LFO2);
        fm1_led_key(BTN_FX, page >= PG_CHORUS && page <= PG_REVERB);
        fm1_led_key(BTN_GLO, page >= PG_VOICE && page <= PG_GLOBAL);
        fm1_led_key(BTN_SAVE, save_armed);
#if BOARD_SMK37
        smk_leds();                             /* FADER BANK, < > */
#endif

#if BOARD_SMK37
        {   /* KNOB BANK held: the panel check owns the screen; let go, everything is drawn again */
            uint32_t redraw = 0;
            if (smk_check(&redraw)) continue;
            if (redraw) {
                lcd_fill(0, 0, 240, 240, C_PANEL);   /* (the strips leave gaps between them) */
                dirty_hdr = dirty_page = dirty_tags = 1; hints_key = leds_q = 0xFFFFFFFFu; scope_state = 1; t_scope = 0;
                smk_ov_dirty = 1;
            }
        }
#endif
        /* the screen: only what changed */
        idle_ok = 1;
        if (dirty_hdr && fm1_ms - t_hdr >= 30u) { dirty_hdr = 0; t_hdr = fm1_ms; draw_header(); }
        if (dirty_page && fm1_ms - t_page >= 30u) { dirty_page = 0; t_page = fm1_ms; draw_page(); }
        if (dirty_tags) { dirty_tags = 0; draw_tags(); }
        if (fm1_ms - t_led >= 40u) {
            uint32_t q = 0;
            t_led = fm1_ms;
            for (k = 0; k < NV; k++) q = q * 9u + ((uint32_t)ui_vlevel[k] >> 12);   /* 8 steps a LED: redraw on a change */
#if BOARD_SMK37
            if (smk_overlay_draw(&leds_q)) ;    /* a fader's bar, a bank */
            else
#endif
            if (layer) draw_hints();
            else if (q != leds_q) { leds_q = q; draw_leds(); }
            if (page == PG_GLOBAL && fm1_ms - t_page >= 250u) dirty_page = 1;
        }
        if (scope_state == 3u && fm1_ms - t_scope >= 50u) {
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
        idle_ok = 0;
        if ((set_dirty || ps_dirty) && engine_quiet() && fm1_ms - set_t > 2000u) {   /* when silent: an erase stalls the sound */
            uint32_t i;
            for (i = 0; i < NPSECT; i++)        /* the programs before the settings (a 1.4 update: they leave the settings) */
                if (ps_dirty >> i & 1u) psect_save(i, &psect[i], sizeof psect[i]);
            ps_dirty = 0;
            if (set_dirty) settings_save(&set, sizeof set);
            set_dirty = 0;
        }
    }
}
