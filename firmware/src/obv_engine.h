/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine: 8 voices in the audio ISR. The main loop sends note events through a ring and writes the live
 * patch and the controllers; the ISR owns the voices, the held-note list, the arpeggiator and the sustain. */
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif
#define NV 8u
#define NHELD 16u
#define ENV_ONE (1 << 24)
#define LEVEL_FLOOR 2048                        /* -78 dB: a release below this frees the voice */

enum { E_IDLE, E_ATT, E_DEC, E_SUS, E_REL };
typedef struct { uint32_t stage; int32_t level; } env_t;   /* level Q24 */

typedef struct {
    osc_t o1, o2;
    uint32_t noise;
    env_t fe, ae;
    svf_state_t f1, f2;
    svf_coef_t c1, c2;
    uint32_t inc1, inc2, pw1, pw2;              /* this block's oscillator settings */
    int32_t amp0, amp1;                         /* Q15 at the block's start and end */
    int32_t pitch16, target16;                  /* Q16 semitones: the glide */
    int32_t det8;                               /* unison / dual offset, Q8 */
    int32_t gl, gr;                             /* pan gains Q12 */
    int32_t drift;                              /* the vintage random walk */
    uint32_t lfo_blk;                           /* blocks since the note on (LFO delay / fade) */
    int32_t lfo_fade;                           /* Q15 */
    uint32_t seq;                               /* allocation order */
    uint8_t note, vel, gate;                    /* gate: the key (or the arp) holds it */
    uint8_t part;                               /* 0: sound A, 1: track B's own sound */
} voice_t;

/* two sounds: A (the keys, the arp, the effects) and track B's own. Track B has one when its pattern names a
 * program (pat.trk[1].pad[0] = program + 1); else B plays A as part 0. The main loop writes, the ISR reads. */
static patch_t live, live_b, live_bq;           /* live_bq: the next pattern's B, swapped in with it */
static volatile uint32_t ctl_bq_swap;           /* the next pattern brings live_bq along */
static uint32_t b_own;                          /* (ISR) track B has its own sound this block */
static voice_t vc[NV];
static uint32_t vseq, rr;                       /* allocation counter, round-robin pointer */

/* main loop -> ISR: note events */
typedef struct { uint8_t type, note, vel, part; } nev_t;   /* type 1 on, 0 off; part 1: track B's sound */
#define NQ 64u
static nev_t nq[NQ];
static volatile uint32_t nq_w, nq_r;
static void note_event_p(uint32_t part, uint32_t on, uint32_t note, uint32_t vel)
{
    if (nq_w - nq_r >= NQ) return;              /* a flood: drop (the ISR drains 64 per block) */
    nq[nq_w % NQ].part = (uint8_t)part;
    nq[nq_w % NQ].type = (uint8_t)on;
    nq[nq_w % NQ].note = (uint8_t)note;
    nq[nq_w % NQ].vel = (uint8_t)vel;
    RING_PUBLISH();
    nq_w++;
}
static void note_event(uint32_t on, uint32_t note, uint32_t vel) { note_event_p(0, on, note, vel); }

/* the controllers (main loop writes) */
static volatile int32_t ctl_bend;               /* -8192..8191 */
static volatile uint32_t ctl_sustain, ctl_hold, ctl_cc7 = 127;   /* ctl_hold: bit p = sound p's HOLD */
#if BOARD_SMK37
static volatile int32_t ctl_vib;                /* the SMK-37's mod wheel 0..127: a vibrato from the LFO, up to +-1/2 semitone */
#endif
static volatile int32_t ctl_tune8;              /* global tune, Q8 semitones */
static volatile uint32_t ctl_tempo = 120, ctl_ext_quarter;   /* MIDI clock: frames per quarter, 0 = none */
static volatile uint32_t ctl_arp_restart, ctl_panic;

/* ISR -> main loop */
static volatile int32_t ui_vlevel[NV];          /* each voice's amp envelope, Q15 */
static volatile uint8_t ui_vnote[NV];
static volatile uint32_t ui_render_us, ui_arp_step;
#define SCOPE_N 240u
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_state, scope_stride = 4;   /* 0 idle, 1 armed, 2 running, 3 done */

/* ---- the held notes (ISR): in order of pressing, physically down or held by the pedal / latch */
static struct { uint8_t note, vel, phys; } held[2][NHELD];   /* per sound: A's keys, B's keys */
static uint32_t nheld[2];
static uint32_t rnd_state = 0x9E3779B9u;

/* ---- the two sounds as the ISR sees them: the patch, its LFO (global per sound, as the originals'), the numbers
 * derived from the patch once per block */
typedef struct {
    const patch_t *p;
    uint32_t lfo_ph, lfo_rnd_a, lfo_rnd_b;
    int32_t lfo_val;                            /* this block, Q15 */
    int32_t last_pitch16;                       /* where a glide starts from */
    int32_t fenv_amt, keytrk, velflt, lfo_pd, lfo_fd, lfo_pwd, lfo_ad, wheel_q, bend8, env_o2, det_fine8;
    int32_t lvl1, lvl2, lvln, drive_q12, reso_k, vint, sus_f, sus_a, gain_q12, morph_q;
    int32_t cut_sm, reso_sm;                    /* CUTOFF and RESO slewed (Q8), ~25 ms: a CC sweep in semitone steps glides */
    uint32_t pw1_base, pw2_base, w1, w2, ftype, sync_on, xmod_q, glide_k;
} part_t;
static part_t pt[2] = {{&live, 0, 0, 0, 0, 60 << 16}, {&live, 0, 0, 0, 0, 60 << 16}};
static uint32_t seq_b_own(void);                /* (obv_seq.h) the pattern gives track B its own sound */
static void parts_point(void)                   /* which patch each part plays (the pattern may have changed) */
{
    uint32_t was = b_own;
    b_own = seq_b_own();
    pt[1].p = b_own ? &live_b : &live;
    if (b_own && !was) {                        /* B's own from now: its cutoff from where it is, not a sweep */
        pt[1].cut_sm = live_b.v[P_CUTOFF] << 8;
        pt[1].reso_sm = live_b.v[P_RESO] << 8;
    }
}

static void held_remove(uint32_t p, uint32_t note)
{
    uint32_t i, j;
    for (i = j = 0; i < nheld[p]; i++)
        if (held[p][i].note != note) held[p][j++] = held[p][i];
    nheld[p] = j;
}

/* ---- vintage: each voice card has its own small offsets, scaled by the knob */
static const int16_t VINT_TUNE8[NV] = {-20, 12, 7, -15, 18, -9, 3, -17};     /* +-8 cents */
static const int16_t VINT_CUT8[NV] = {-300, 250, 120, -380, 60, 330, -180, 220};   /* +-1.5 semitones */
static const int8_t VINT_ENV[NV] = {10, -6, 14, -12, 3, -16, 8, -4};         /* /128: +-12 % on the times */
static const int8_t PAN_CARD[NV] = {-40, 40, -24, 24, -56, 56, -10, 10};     /* the originals' pan pots */

static void voice_pan(voice_t *v, int32_t pan)   /* -64..64 */
{
    uint32_t pos = (uint32_t)(clampi(pan, -64, 64) + 64);
    v->gl = PAN_Q12[128u - pos];
    v->gr = PAN_Q12[pos];
}

static void voice_start(voice_t *v, uint32_t part, uint32_t note, uint32_t vel, int32_t det8, int32_t pan)
{
    const uint8_t *pv = pt[part].p->v;
    v->part = (uint8_t)part;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->seq = ++vseq;
    v->det8 = det8;
    v->target16 = (int32_t)note << 16;
    v->pitch16 = pv[P_GLIDE] ? pt[part].last_pitch16 : v->target16;
    v->fe.stage = E_ATT;
    v->ae.stage = E_ATT;
    v->lfo_blk = 0;
    v->lfo_fade = pv[P_LFO_DELAY] ? 0 : 32767;
    voice_pan(v, pan);
}

/* the cards of a part's MONO / UNISON group: all 8, or with two sounds a half each (A 1-4, B 5-8) */
static void part_cards(uint32_t part, uint32_t *lo, uint32_t *n)
{
    *lo = b_own && part ? NV / 2u : 0u;
    *n = b_own ? NV / 2u : NV;
}
static int part_grouped(uint32_t part)
{
    uint32_t m = pt[part].p->v[P_MODE];
    return m == M_MONO || m == M_UNISON;
}

/* round robin over the cards; steal the oldest released, then the oldest. With two sounds both share the 8,
 * unless the other one is a MONO / UNISON group: then it keeps its half */
static uint32_t voice_alloc(uint32_t part)
{
    uint32_t i, v, lo = 0, n = NV, pick, best = 0xFFFFFFFFu;
    if (b_own && part_grouped(part ^ 1u)) part_cards(part, &lo, &n);
    pick = lo;
    for (i = 0; i < n; i++) {
        v = lo + (rr + i) % n;
        if (vc[v].ae.stage == E_IDLE) { rr = v + 1u; return v; }
    }
    for (v = lo; v < lo + n; v++)
        if (!vc[v].gate && vc[v].seq < best) { best = vc[v].seq; pick = v; }
    if (best == 0xFFFFFFFFu)
        for (v = lo; v < lo + n; v++)
            if (vc[v].seq < best) { best = vc[v].seq; pick = v; }
    rr = pick + 1u;
    return pick;
}

static uint32_t uni_count(uint32_t part)
{
    uint32_t m = pt[part].p->v[P_MODE], lo, n;
    part_cards(part, &lo, &n);
    return m == M_MONO ? 1u : m == M_UNISON ? clampi(pt[part].p->v[P_UNI_N], 1, (int32_t)n) : 0u;
}

static void voice_trigger(uint32_t part, uint32_t note, uint32_t vel)
{
    const uint8_t *pv = pt[part].p->v;
    uint32_t mode = pv[P_MODE], n, i, lo, gn, legato = 0;
    int32_t det = (pv[P_UNI_DET] * pv[P_UNI_DET]) >> 6, spread = pv[P_SPREAD];
    if (mode == M_POLY) {
        uint32_t v = voice_alloc(part);
        voice_start(&vc[v], part, note, vel, 0, (PAN_CARD[v] * spread) >> 7);
    } else if (mode == M_DUAL) {
        uint32_t a = voice_alloc(part), b = voice_alloc(part);
        voice_start(&vc[a], part, note, vel, -det / 2, (-48 * spread) >> 7);
        voice_start(&vc[b], part, note, vel, det / 2, (48 * spread) >> 7);
    } else {
        n = uni_count(part);
        part_cards(part, &lo, &gn);
        for (i = 0; i < n; i++)
            if (vc[lo + i].gate && vc[lo + i].part == part) legato = 1;
        for (i = 0; i < n; i++) {
            voice_t *v = &vc[lo + i];
            int32_t d = n > 1 ? (det * (int32_t)(2u * i) / (int32_t)(n - 1u)) - det : 0;
            int32_t pan = n > 1 ? ((int32_t)(128u * i) / (int32_t)(n - 1u) - 64) * spread >> 7 : 0;
            if (legato && v->part == part) {    /* a key still down: slide, no retrigger (a card of the other sound's: a new start) */
                v->note = (uint8_t)note;
                v->target16 = (int32_t)note << 16;
                if (!pv[P_GLIDE]) v->pitch16 = v->target16;
                v->det8 = d;
                v->gate = 1;
                voice_pan(v, pan);
            } else
                voice_start(v, part, note, vel, d, pan);
        }
        for (i = n; i < gn; i++)                /* the cards beyond the group fall silent */
            vc[lo + i].gate = 0;
    }
    pt[part].last_pitch16 = (int32_t)note << 16;
    if (pv[P_LFO_PHASE] == LP_SYNC && !legato) {
        for (i = 0; i < NV; i++)
            if (vc[i].gate && vc[i].part == part && vc[i].note != note) break;
        if (i == NV) pt[part].lfo_ph = 0;       /* the first key of a chord restarts the LFO */
    }
}

/* the key (or the arp, the sequencer) let go of a note. In MONO / UNISON a key still held takes over (legato) */
static void voice_release(uint32_t part, uint32_t note, uint32_t from_arp)
{
    const uint8_t *pv = pt[part].p->v;
    uint32_t v, mode = pv[P_MODE];
    if ((mode == M_MONO || mode == M_UNISON) && !from_arp && nheld[part] && pv[P_ARP_MODE] == A_OFF) {
        uint32_t n = uni_count(part), i, lo, gn;
        part_cards(part, &lo, &gn);
        if (vc[lo].gate && vc[lo].part == part && vc[lo].note == note) {
            for (i = 0; i < n; i++) {
                if (vc[lo + i].part != part) continue;   /* (the other sound's card: its note stays its own) */
                vc[lo + i].note = held[part][nheld[part] - 1u].note;
                vc[lo + i].target16 = (int32_t)vc[lo + i].note << 16;
                if (!pv[P_GLIDE]) vc[lo + i].pitch16 = vc[lo + i].target16;
            }
            pt[part].last_pitch16 = vc[lo].target16;
            for (v = 0; v < NV; v++)            /* (a card outside the group: begun before a MODE / program change) */
                if ((v < lo || v >= lo + n) && vc[v].gate && vc[v].note == note && vc[v].part == part) vc[v].gate = 0;
            return;
        }
    }                                           /* (not the group's note: let it go wherever it sounds) */
    for (v = 0; v < NV; v++)
        if (vc[v].gate && vc[v].note == note && vc[v].part == part) vc[v].gate = 0;
}

static void voices_release_all(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) vc[v].gate = 0;
}
static void voices_release_part(uint32_t part)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (vc[v].part == part) vc[v].gate = 0;
}

static void seq_tick(void);                     /* the step sequencer (obv_seq.h): per block, after the arp */

/* the click (main: ctl_click on / off) and a one-bar count-in before a recording (main: ctl_count) */
static volatile uint32_t ctl_click, ctl_count, ui_seq_count;
static int32_t click_amp;
static uint32_t click_ph, click_inc;
static void click_start(uint32_t accent)        /* beat 1 higher and louder */
{
    click_amp = accent ? 9000 : 6000;
    click_ph = 0;
    click_inc = accent ? 233739600u : 155826400u;   /* 2400 / 1600 Hz in 2^32 / 44100 steps */
}

/* ---- the arpeggiators (per block, 0.36 ms steps): one per sound, each on its own keys and its own ARP settings */
typedef struct { uint32_t cnt, period, gate_at, pos, dir, sounding, note, was_on; } arp_t;
static arp_t arp[2];

/* ISR -> main loop: the notes the arps play (REC records them, not the keys that feed them): on, note, part */
#define AQ 32u
static uint8_t arp_q[AQ][3];
static volatile uint32_t arp_qw, arp_qr;
static void arp_post(uint32_t p, uint32_t on, uint32_t note)
{
    if (arp_qw - arp_qr >= AQ) return;
    arp_q[arp_qw % AQ][0] = (uint8_t)on;
    arp_q[arp_qw % AQ][1] = (uint8_t)note;
    arp_q[arp_qw % AQ][2] = (uint8_t)p;
    RING_PUBLISH();
    arp_qw++;
}

static void arp_stop_note(uint32_t p)
{
    arp_t *a = &arp[p];
    if (a->sounding) { voice_release(p, a->note, 1); a->sounding = 0; arp_post(p, 0, a->note); }
}

static void arp_step(uint32_t p)
{
    const uint8_t *pv = pt[p].p->v;
    arp_t *a = &arp[p];
    uint8_t sorted[NHELD];
    uint32_t i, j, n = nheld[p], oct = clampi(pv[P_ARP_OCT], 1, 4), len, k, mode = pv[P_ARP_MODE], idx;
    for (i = 0; i < n; i++) sorted[i] = held[p][i].note;
    if (mode != A_PLAY)
        for (i = 1; i < n; i++) {               /* insertion sort: 16 at most */
            uint8_t t = sorted[i];
            for (j = i; j && sorted[j - 1u] > t; j--) sorted[j] = sorted[j - 1u];
            sorted[j] = t;
        }
    len = n * oct;
    if (mode == A_RND)
        k = xorshift(&rnd_state) % len;
    else if (mode == A_UPDN && len > 2u) {
        if (a->pos >= 2u * len - 2u) a->pos = 0;
        k = a->pos < len ? a->pos : 2u * len - 2u - a->pos;
        a->pos++;
    } else {
        if (a->pos >= len) a->pos = 0;
        k = mode == A_DOWN ? len - 1u - a->pos : a->pos;
        a->pos++;
    }
    idx = k % n;
    a->note = (uint32_t)clampi((int32_t)sorted[idx] + 12 * (int32_t)(k / n), 0, 127);
    for (i = 0; i < n; i++)                     /* the velocity of that key */
        if (held[p][i].note == sorted[idx]) break;
    voice_trigger(p, a->note, i < n ? held[p][i].vel : 100u);
    arp_post(p, 1, a->note);
    a->sounding = 1;
    ui_arp_step++;
}

static void arp_tick(uint32_t p)
{
    const uint8_t *pv = pt[p].p->v;
    arp_t *a = &arp[p];
    uint32_t mode = pv[P_ARP_MODE], div = DIV_PER_QUARTER[pv[P_ARP_DIV] % NDIV], q;
    if (mode == A_OFF) {
        if (a->was_on) { arp_stop_note(p); a->was_on = 0; }
        return;
    }
    if (!a->was_on) { voices_release_part(p); a->was_on = 1; a->pos = 0; a->cnt = 0xFFFFFFFFu; }
    if (!nheld[p]) { arp_stop_note(p); a->pos = 0; a->cnt = 0xFFFFFFFFu; return; }
    q = ctl_ext_quarter ? ctl_ext_quarter : 60u * FS / clampi(ctl_tempo, 40, 240);
    a->period = q / div;
    if (a->period < CTL) a->period = CTL;
    a->gate_at = (a->period * (pv[P_ARP_GATE] + 1u)) >> 7;
    if (a->cnt == 0xFFFFFFFFu || a->cnt + CTL >= a->period) {
        a->cnt = a->cnt == 0xFFFFFFFFu ? 0 : a->cnt + CTL - a->period;
        arp_stop_note(p);
        arp_step(p);
    } else {
        a->cnt += CTL;
        if (a->sounding && a->cnt >= a->gate_at && pv[P_ARP_GATE] < 127u) arp_stop_note(p);
    }
}

/* ---- note events (ISR), per sound: its held keys, its arp, its HOLD; the pedal holds both */
static uint32_t sus_was;                        /* bit p: sound p was held */
static inline uint32_t part_sus(uint32_t p) { return ctl_sustain || (ctl_hold >> p & 1u); }
static void ev_note_on(uint32_t p, uint32_t note, uint32_t vel)
{
    uint32_t i, arp_on = pt[p].p->v[P_ARP_MODE] != A_OFF, sus = part_sus(p);
    if (arp_on && sus) {                        /* latch: a new chord after all keys were up starts over */
        for (i = 0; i < nheld[p]; i++)
            if (held[p][i].phys) break;
        if (i == nheld[p]) { nheld[p] = 0; arp[p].pos = 0; arp[p].cnt = 0xFFFFFFFFu; }
    }
    held_remove(p, note);
    if (nheld[p] == NHELD) { for (i = 1; i < NHELD; i++) held[p][i - 1u] = held[p][i]; nheld[p]--; }
    held[p][nheld[p]].note = (uint8_t)note;
    held[p][nheld[p]].vel = (uint8_t)vel;
    held[p][nheld[p]].phys = 1;
    nheld[p]++;
    if (arp_on) {
        if (nheld[p] == 1u) { arp[p].pos = 0; arp[p].cnt = 0xFFFFFFFFu; }   /* the first key: a step right away */
        return;
    }
    voice_trigger(p, note, vel);
}

static void ev_note_off(uint32_t p, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < nheld[p]; i++)
        if (held[p][i].note == note) held[p][i].phys = 0;
    if (part_sus(p)) return;                    /* the pedal / its HOLD keeps it */
    held_remove(p, note);
    if (pt[p].p->v[P_ARP_MODE] != A_OFF) return;
    voice_release(p, note, 0);
}

static void ev_sustain_up(uint32_t p)           /* the pedal / its HOLD let go: what no key holds is released */
{
    uint32_t i, j;
    {
        uint8_t gone[NHELD], ng = 0;
        for (i = j = 0; i < nheld[p]; i++)
            if (held[p][i].phys) held[p][j++] = held[p][i];
            else gone[ng++] = held[p][i].note;
        nheld[p] = j;
        if (pt[p].p->v[P_ARP_MODE] != A_OFF) return;
        for (i = 0; i < ng; i++) voice_release(p, gone[i], 0);
    }
}

static void parts_off(uint32_t p)               /* a sound gone (B back on A, a panic): its keys, its arp, its notes */
{
    nheld[p] = 0;
    arp_stop_note(p);
    arp[p].was_on = 0;
    voices_release_part(p);
}

static void engine_events(void)
{
    uint32_t sus = part_sus(0) | part_sus(1) << 1;
    if (ctl_panic) { ctl_panic = 0; parts_off(0); parts_off(1); voices_release_all(); }
    if (sus_was & ~sus & 1u) ev_sustain_up(0);  /* each sound on its own: A's HOLD off never touches B */
    if (sus_was & ~sus & 2u) ev_sustain_up(1);
    sus_was = sus;
    while (nq_r != nq_w) {
        static uint8_t on_p[2][128];            /* the sound a note began on + 1: B's own sound may come or go meanwhile */
        nev_t e = nq[nq_r % NQ];
        uint32_t p = e.part && b_own, q = e.part & 1u, n = e.note & 127u;   /* (B without its own sound: its keys play A) */
        nq_r++;
        if (e.type) { on_p[q][n] = (uint8_t)(p + 1u); ev_note_on(p, e.note, e.vel); }
        else {
            if (on_p[q][n]) p = on_p[q][n] - 1u;
            on_p[q][n] = 0;
            ev_note_off(p, e.note);
        }
    }
}

/* ---- envelopes: analog RC curves per block (CTL frames), level Q24 */
static inline void env_tick(env_t *e, uint32_t a, uint32_t d, int32_t sus, uint32_t r, int32_t gate, int32_t vint)
{
    int32_t k, target;
    if (gate && e->stage >= E_REL) e->stage = E_ATT;
    if (!gate && e->stage && e->stage < E_REL) e->stage = E_REL;
    switch (e->stage) {
    case E_ATT:
        k = (int32_t)ENV_K_ATT[a];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        target = ENV_ONE + (ENV_ONE * 3 >> 3);  /* aims at 1.3, stops at 1.0 */
        e->level += (int32_t)(((int64_t)(target - e->level) * k) >> 24);
        if (e->level >= ENV_ONE) { e->level = ENV_ONE; e->stage = E_DEC; }
        break;
    case E_DEC:
        k = (int32_t)ENV_K_DEC[d];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(sus - e->level) * k) >> 24) - 16;
        if (e->level <= sus + 64) { e->level = sus; e->stage = E_SUS; }
        break;
    case E_SUS:
        e->level = sus;                         /* the knob moves it */
        break;
    case E_REL:
        k = (int32_t)ENV_K_DEC[r];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(0 - e->level) * k) >> 24) - 32;
        if (e->level < LEVEL_FLOOR) { e->level = 0; e->stage = E_IDLE; }
        break;
    default:
        e->level = 0;
    }
}

/* ---- the LFO (global; per voice a phase offset in SPREAD and a delay / fade) */
static int32_t lfo_shape(const part_t *g, uint32_t ph, uint32_t shape)
{
    int32_t p;
    switch (shape) {
    case L_TRI:
        p = (int32_t)((ph + 0x40000000u) >> 15);
        return p < 65536 ? p - 32768 : 98303 - p;
    case L_SAW: return 32767 - (int32_t)(ph >> 16);
    case L_RAMP: return (int32_t)(ph >> 16) - 32768;
    case L_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case L_SH: return (int32_t)(g->lfo_rnd_b >> 16) - 32768;
    default: {                                  /* smooth random: a line from one random value to the next */
        int32_t a = (int32_t)(g->lfo_rnd_a >> 16) - 32768, b = (int32_t)(g->lfo_rnd_b >> 16) - 32768;
        return a + (int32_t)(((int64_t)(b - a) * (ph >> 8)) >> 24);
    }
    }
}

static void lfo_tick(part_t *g)
{
    uint32_t inc = LFO_INC[g->p->v[P_LFO_RATE]], old = g->lfo_ph;
    g->lfo_ph += inc;
    if (g->lfo_ph < old) { g->lfo_rnd_a = g->lfo_rnd_b; g->lfo_rnd_b = xorshift(&rnd_state); }
    g->lfo_val = lfo_shape(g, g->lfo_ph, g->p->v[P_LFO_SHAPE]);
}

/* ---- one control block for a voice: pitch, cutoff, envelopes, amp */
static int32_t vol_q12;                         /* the output stage: CC 7 (each sound's VOLUME is in its voices) */

static void block_globals(part_t *g)            /* a sound's derived numbers, once per block */
{
    const uint8_t *pv = g->p->v;
    int32_t d;
    g->fenv_amt = (int32_t)pv[P_FENV_AMT] - 64;
    g->keytrk = pv[P_KEYTRK];
    g->velflt = pv[P_VEL_FLT];
    d = pv[P_LFO_PITCH]; g->lfo_pd = (d * d) >> 7;
    d = pv[P_LFO_FLT]; g->lfo_fd = (d * d) >> 7;
    d = pv[P_LFO_PWM]; g->lfo_pwd = (d * d) >> 7;
    g->lfo_ad = pv[P_LFO_AMP];
    g->wheel_q = pv[P_MOD_DEPTH];               /* the knob and the mod wheel (CC 1) are the same value */
    g->bend8 = (ctl_bend * pv[P_BEND] * 256) >> 13;
    g->env_o2 = (int32_t)pv[P_ENV_O2] - 64;
    g->det_fine8 = ((int32_t)pv[P_O2_FINE] - 64) * 2;
    d = pv[P_O1_LVL]; g->lvl1 = (d * d) >> 7;
    d = pv[P_O2_LVL]; g->lvl2 = (d * d) >> 7;
    d = pv[P_NOISE]; g->lvln = (d * d) >> 7;
    d = pv[P_DRIVE]; g->drive_q12 = d ? 4096 + (d * d * 7 * 4096 / 16129) : 0;
    g->cut_sm += ((pv[P_CUTOFF] << 8) - g->cut_sm) >> 6;   /* tau = 64 blocks = 23 ms */
    g->reso_sm += ((pv[P_RESO] << 8) - g->reso_sm) >> 6;
    g->reso_k = 8192 - g->reso_sm * 8400 / (127 * 256);
    g->vint = pv[P_VINTAGE];
    g->sus_f = pv[P_FS] * (ENV_ONE / 127);
    g->sus_a = pv[P_AS] * (ENV_ONE / 127);
    d = pv[P_VOLUME];
    g->gain_q12 = (d * d) >> 2;                 /* (x CC 7 at the output: as VOLUME x CC 7 squared was) */
    g->pw1_base = ((uint32_t)pv[P_O1_PW] << 25) + (1u << 24);
    g->pw2_base = ((uint32_t)pv[P_O2_PW] << 25) + (1u << 24);
    g->w1 = pv[P_O1_WAVE];
    g->w2 = pv[P_O2_WAVE];
    g->ftype = pv[P_FTYPE];
    g->morph_q = pv[P_FMORPH];                  /* SEM: 0 LP, 64 notch, 127 HP */
    g->sync_on = pv[P_SYNC];
    d = pv[P_XMOD]; g->xmod_q = (uint32_t)((d * d) >> 7);
    g->glide_k = GLIDE_K[pv[P_GLIDE]];
}

static inline uint32_t pw_mod(uint32_t base, int32_t mod)
{
    int64_t p = (int64_t)base + ((int64_t)mod << 8);
    if (p < (1 << 26)) p = 1 << 26;
    if (p > (int64_t)0xFFFFFFFFu - (1 << 26)) p = (int64_t)0xFFFFFFFFu - (1 << 26);
    return (uint32_t)p;
}

static void voice_block(voice_t *v, uint32_t card)
{
    const part_t *g = &pt[v->part];
    const uint8_t *pv = g->p->v;
    int32_t lfo, p8, p1, p2, cut, fenv, tmp, velg, trem, vint = g->vint, vt = (vint * VINT_ENV[card]) >> 7, lfo_to, a;
    uint32_t ph, glide_k = g->glide_k;
    /* glide */
    if (v->pitch16 != v->target16) {
        if (!glide_k) v->pitch16 = v->target16;
        else {
            int32_t diff = v->target16 - v->pitch16;
            v->pitch16 += (int32_t)(((int64_t)diff * glide_k) >> 16);
            if (diff > -64 && diff < 64) v->pitch16 = v->target16;
        }
    }
    /* envelopes */
    env_tick(&v->fe, pv[P_FA], pv[P_FD], g->sus_f, pv[P_FR], v->gate, vt);
    env_tick(&v->ae, pv[P_AA], pv[P_AD], g->sus_a, pv[P_AR], v->gate, vt);
    fenv = v->fe.level >> 9;                    /* Q15 */
    /* the LFO as this voice sees it */
    ph = g->lfo_ph;
    if (pv[P_LFO_PHASE] == LP_SPREAD) ph += card << 29;
    lfo = pv[P_LFO_PHASE] == LP_SPREAD ? lfo_shape(g, ph, pv[P_LFO_SHAPE]) : g->lfo_val;
    if (v->lfo_fade < 32767) {
        uint32_t dl = LFO_DELAY_BLK[pv[P_LFO_DELAY]];
        if (v->lfo_blk >= dl) { v->lfo_fade += 32767 / (int32_t)(dl / 2u + 1u) + 1; if (v->lfo_fade > 32767) v->lfo_fade = 32767; }
        v->lfo_blk++;
    }
    lfo = (lfo * v->lfo_fade) >> 15;
    /* vintage: a slow random walk on the pitch */
    v->drift += (int32_t)(xorshift(&v->noise) & 31u) - 16;
    v->drift -= v->drift >> 11;
    /* pitch, Q8 semitones */
    p8 = (v->pitch16 >> 8) + g->bend8 + ctl_tune8 + v->det8 + ((VINT_TUNE8[card] * vint) >> 7) + ((v->drift * vint) >> 10);
    p8 += (lfo * g->wheel_q * 4) >> 15;         /* the wheel's vibrato */
#if BOARD_SMK37
    p8 += (lfo * ctl_vib) >> 15;                /* the SMK-37's mod wheel: pitch only, gentle */
#endif
    lfo_to = (lfo * g->lfo_pd * 24) >> 15;
    p1 = p8 + (pv[P_LFO_PITCH_TO] != TO_O2 ? lfo_to : 0);
    p2 = p8 + (pv[P_LFO_PITCH_TO] != TO_O1 ? lfo_to : 0) + ((int32_t)pv[P_O2_SEMI] - 24) * 256 + g->det_fine8
       + ((fenv * g->env_o2 * 97) >> 15);
    v->inc1 = pitch_inc(p1);
    v->inc2 = pitch_inc(p2);
    tmp = (lfo * g->lfo_pwd);
    v->pw1 = pw_mod(g->pw1_base, pv[P_LFO_PWM_TO] != TO_O2 ? tmp : 0);
    v->pw2 = pw_mod(g->pw2_base, pv[P_LFO_PWM_TO] != TO_O1 ? tmp : 0);
    /* cutoff, Q8 semitones */
    cut = g->cut_sm + 8 * 256 + ((fenv * g->fenv_amt * 390) >> 15)
        + ((int32_t)v->note - 60) * g->keytrk * 2 + (((int32_t)v->vel - 64) * g->velflt >> 1)
        + ((lfo * g->lfo_fd * 97) >> 15) + ((lfo * g->wheel_q * 48) >> 15) + ((VINT_CUT8[card] * vint) >> 7);
    svf_coef(&v->c1, cut, g->reso_k);
    if (g->ftype == F_LP24) svf_coef(&v->c2, cut, 5793);
    /* amp */
    velg = 32767 - ((((127 - (int32_t)v->vel) * (int32_t)pv[P_VEL_AMP]) >> 7) * 258);
    trem = 32767 - (((32767 - lfo) * g->lfo_ad) >> 8);
    v->amp0 = v->amp1;
    a = (int32_t)(((int64_t)(v->ae.level >> 9) * velg >> 15) * trem >> 15);
    v->amp1 = (a * g->gain_q12) >> 12;          /* the sound's VOLUME */
    ui_vlevel[card] = a;
    ui_vnote[card] = v->note;
}

/* ---- the samples of one voice: oscillators -> mix -> drive -> filter -> amp -> pan */
static void voice_render(voice_t *v, int32_t *out)
{
    uint32_t i, inc1 = v->inc1, inc2 = v->inc2, pw1 = v->pw1, pw2 = v->pw2, d1 = inc1 >> 15, d2 = inc2 >> 15;
    int32_t amp0 = v->amp0, da = v->amp1 - v->amp0, gl = v->gl, gr = v->gr, o2n = v->o2.prev;
    const part_t *g = &pt[v->part];
    int32_t l1 = g->lvl1, l2 = g->lvl2, ln = g->lvln, drv = g->drive_q12, morph_q = g->morph_q;
    uint32_t ft = g->ftype, xq = g->xmod_q, sy = g->sync_on, ww1 = g->w1, ww2 = g->w2;
    int32_t hw1 = (ww1 & OSC_SAW ? -65536 : 0) + (ww1 & OSC_PLS ? -65536 : 0);
    int32_t hw2 = (ww2 & OSC_SAW ? -65536 : 0) + (ww2 & OSC_PLS ? -65536 : 0);
    for (i = 0; i < CTL; i++) {
        int32_t s1, s2, mix, lp, bp, hp, y, amp, rt = -1;
        uint32_t ie = inc1;
        if (xq) {                               /* OSC2 -> OSC1 linear FM, the step never below inc/16 */
            int32_t m = (int32_t)(inc1 >> 8 > 0x200000u ? 0x200000u : inc1 >> 8) * (((o2n >> 8) * (int32_t)xq) >> 6);
            m += (int32_t)inc1;
            ie = m < (int32_t)(inc1 >> 4) ? inc1 >> 4 : (uint32_t)m;
            d1 = ie >> 15;
        }
        s1 = osc_step(&v->o1, ie, d1, pw1, ww1, hw1, -1);
        if (sy && v->o1.ph < ie && d1)
            rt = (int32_t)(v->o1.ph / d1);
        s2 = osc_step(&v->o2, inc2, d2, pw2, ww2, hw2, rt);
        o2n = v->o2.prev;
        mix = s1 * l1 + s2 * l2;
        if (ln) mix += ((int32_t)(xorshift(&v->noise) >> 16) - 32768) * ln;
        mix >>= 7;
        if (drv) mix = softclip(((mix >> 3) * drv) >> 9);
        else mix = clampi(mix, -65535, 65535);
        svf_step(&v->c1, &v->f1, mix, &lp, &bp, &hp);
        switch (ft) {
        case F_LP12: y = lp; break;
        case F_HP12: y = hp; break;
        case F_BP12: y = bp; break;
        case F_NOTCH: y = hp + lp; break;
        case F_SEM: y = lp + (((hp - lp) * morph_q) >> 7); break;   /* the SEM's LP .. notch .. HP pot */
        default: y = svf_lp(&v->c2, &v->f2, lp); break;
        }
        y = clampi(y, -65000, 65000);
        amp = amp0 + ((da * (int32_t)i) >> 4);
        y = (y * amp) >> 16;
        out[2u * i] += (y * gl) >> 12;
        out[2u * i + 1u] += (y * gr) >> 12;
    }
    (void)o2n;
}

static uint32_t seq_b_dry(void);                /* (obv_seq.h) how much of track B's sound passes the effects by, 0..127 */
static int32_t bus_b[2u * HALF_FRAMES];         /* track B's own sound: into the effects by its send, the rest dry */

static void engine_audio(int32_t *out, uint32_t n)
{
    uint32_t b, v, i, was_own = b_own, dry, bus = b_own;
    static int32_t sc_prev;
    static uint32_t sc_i, sc_sub;
    fx_block(&live, ctl_ext_quarter ? ctl_ext_quarter : 60u * FS / clampi(ctl_tempo, 40, 240));
    vol_q12 = (int32_t)((ctl_cc7 * ctl_cc7) >> 2);
    for (v = 0; v < NV; v++)                    /* (one sound and no B tails: no bus) */
        if (vc[v].part && vc[v].ae.stage != E_IDLE) bus = 1;
    if (bus)
        for (i = 0; i < 2u * n; i++) bus_b[i] = 0;
    for (b = 0; b < n; b += CTL) {
        parts_point();
        if (was_own && !b_own) parts_off(1);    /* (B back on sound A: its own notes let go) */
        engine_events();
        if (ctl_arp_restart) { ctl_arp_restart = 0; arp[0].pos = arp[1].pos = 0; arp[0].cnt = arp[1].cnt = 0xFFFFFFFFu; }
        arp_tick(0);
        if (b_own) arp_tick(1);
        seq_tick();                             /* (may swap the pattern in, and B's sound with it) */
        parts_point();
        if (was_own && !b_own) parts_off(1);
        was_own = b_own;
        lfo_tick(&pt[0]);
        if (b_own) lfo_tick(&pt[1]);
        block_globals(&pt[0]);
        if (b_own) block_globals(&pt[1]);
        for (v = 0; v < NV; v++) {
            if (vc[v].ae.stage == E_IDLE && !vc[v].gate) {
                if (ui_vlevel[v]) { ui_vlevel[v] = 0; vc[v].amp0 = vc[v].amp1 = 0; }
                continue;
            }
            voice_block(&vc[v], v);
            voice_render(&vc[v], (vc[v].part && bus ? bus_b : out) + 2u * b);
        }
    }
    dry = bus ? seq_b_dry() : 0u;
    for (i = 0; bus && i < 2u * n; i++) {              /* B's send: that much joins A on its way into the effects */
        int32_t x = bus_b[i], d = (x * (int32_t)dry) >> 7;
        out[i] += x - d;
        bus_b[i] = d;
    }
    for (i = 0; i < n; i++) {                   /* the output stage: a gentle tanh, never a hard clip */
        int32_t l = softclip((clampi(out[2u * i], -262144, 262143) >> 2) * vol_q12 >> 10);
        int32_t r = softclip((clampi(out[2u * i + 1u], -262144, 262143) >> 2) * vol_q12 >> 10);
        int32_t m = (l + r) >> 1;
        out[2u * i] = l;
        out[2u * i + 1u] = r;
        if (scope_state == 1u) {                /* armed: wait for a rising zero crossing */
            if (sc_prev < 0 && m >= 0) { scope_state = 2; sc_i = 0; sc_sub = 0; }
        } else if (scope_state == 2u) {
            if (!sc_sub++) scope[sc_i++] = (int16_t)m;
            if (sc_sub >= scope_stride) sc_sub = 0;
            if (sc_i >= SCOPE_N) scope_state = 3;
        }
        sc_prev = m;
    }
    fx_process(out, n);
    if (dry)                                    /* B's dry part: its own output stage, after the effects */
        for (i = 0; i < 2u * n; i++)
            out[i] = sat16(out[i] + softclip((clampi(bus_b[i], -262144, 262143) >> 2) * vol_q12 >> 10));
    for (i = 0; i < n && click_amp; i++) {     /* the click: after the effects, never recorded */
        int32_t c;
        click_ph += click_inc;
        c = click_ph & 0x80000000u ? click_amp : -click_amp;
        out[2u * i] += c;
        out[2u * i + 1u] += c;
        click_amp -= (click_amp >> 8) + 1;      /* ~6 ms */
        if (click_amp < 64) click_amp = 0;
    }
}

static int engine_quiet(void)                   /* nothing sounds: a flash erase is safe */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (vc[v].ae.stage != E_IDLE || vc[v].gate) return 0;
    return nq_r == nq_w;
}

static void engine_init(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) {
        vc[v].noise = 0x12345678u * (v + 1u) + 0x9E3779B9u;
        vc[v].o1.ph = xorshift(&vc[v].noise);  /* free-running oscillators start anywhere */
        vc[v].o2.ph = xorshift(&vc[v].noise);
        vc[v].gl = vc[v].gr = 2896;
    }
    pt[0].lfo_rnd_a = xorshift(&rnd_state);
    pt[0].lfo_rnd_b = xorshift(&rnd_state);
    pt[1].lfo_rnd_a = 0x6A09E667u;              /* (B's own: rnd_state's order stays as with one sound) */
    pt[1].lfo_rnd_b = 0xBB67AE85u;
    fx_init();
}
