/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects after the output stage: a stereo chorus, a ping-pong delay in note values of the tempo, and a
 * plate reverb (J. Dattorro's figure-of-eight tank, 1997). All lines are int16 in one .pool buffer; the
 * signals are Q15 as the output stage leaves them. Integers only.
 * REV_DIV 1: the reverb at the full rate (the sound comes first). 2: at half the rate, if the chip turns out
 * to need it: a dark hall loses little by it and it halves the reverb's time and memory. */
#ifndef REV_DIV
#define REV_DIV 1u
#endif
#define RVL(x) ((uint32_t)((x) * 44100u / 29761u / REV_DIV))   /* the paper's 29761 Hz lengths at our rate */
#define DLY_MAX 32768u                          /* a side of the delay: 743 ms */
#define CHO_N 1024u
#define REV_PRE_N (8192u / REV_DIV)             /* up to 120 ms of pre-delay */
#define RV_IN1 RVL(142)
#define RV_IN2 RVL(107)
#define RV_IN3 RVL(379)
#define RV_IN4 RVL(277)
#define RV_AP1 RVL(672)
#define RV_D1 RVL(4453)
#define RV_AP2 RVL(1800)
#define RV_D2 RVL(3720)
#define RV_AP3 RVL(908)
#define RV_D3 RVL(4217)
#define RV_AP4 RVL(2656)
#define RV_D4 RVL(3163)
#define REV_N (REV_PRE_N + RV_IN1 + RV_IN2 + RV_IN3 + RV_IN4 + RV_AP1 + 16u + RV_D1 + RV_AP2 + RV_D2 + RV_AP3 + 16u + RV_D3 + RV_AP4 + RV_D4)
#define RV_BW (REV_DIV == 2u ? 24000 : 16700)   /* the input's one-pole: ~5 kHz either way */
#define FX_MEM_N (CHO_N + 2u * DLY_MAX + REV_N)

typedef struct { int16_t *b; uint32_t len, w; } line_t;   /* w: where the next sample goes */
static int16_t fx_mem[FX_MEM_N] __attribute__((section(".pool")));
static uint32_t fx_used;

static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
static void line_init(line_t *l, uint32_t len) { l->b = fx_mem + fx_used; l->len = len; l->w = 0; fx_used += len; }
static inline __attribute__((always_inline)) int32_t line_read(const line_t *l, uint32_t back)   /* 1 <= back <= len */
{
    uint32_t i = l->w + l->len - back;
    if (i >= l->len) i -= l->len;
    return l->b[i];
}
static inline __attribute__((always_inline)) void line_write(line_t *l, int32_t v)
{
    l->b[l->w] = (int16_t)sat16(v);
    if (++l->w >= l->len) l->w = 0;
}
/* Dattorro's allpass: the line holds the inner node, the output is tapped from it. g Q15, signed */
static inline __attribute__((always_inline)) int32_t ap_step(line_t *l, int32_t in, int32_t g)
{
    int32_t d = line_read(l, l->len), v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
/* the same with the delay swaying by e (Q8 samples, |e| <= 8 << 8) around base; the line is base + 16 long */
static inline __attribute__((always_inline)) int32_t ap_mod(line_t *l, int32_t in, int32_t g, uint32_t base, int32_t e)
{
    uint32_t back = (uint32_t)((int32_t)base + (e >> 8));
    int32_t f = e & 255, a = line_read(l, back), b = line_read(l, back + 1u), d = a + (((b - a) * f) >> 8), v;
    v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
static inline int32_t tri_q(uint32_t ph)        /* a triangle -2048..2047 (so Q8 = +-8 samples) */
{
    int32_t t = (int32_t)(ph >> 19);            /* 0..8191 */
    return t < 4096 ? t - 2048 : 6143 - t;
}

/* ---- the state */
static line_t cho, dl, dr, rv_pre, rv_in[4], rv_ap1, rv_d1, rv_ap2, rv_d2, rv_ap3, rv_d3, rv_ap4, rv_d4;
static uint32_t cho_ph, cho_inc, rv_ph1, rv_ph2;
static int32_t cho_depth_k, cho_mix, dly_mix, rev_mix, dly_fb, dly_tone_k, rev_decay, rev_damp_k;
static int32_t dly_time, dly_cur;               /* Q16 samples: the target and where the read is (it slews like a tape) */
static int32_t dly_lpl, dly_lpr, rv_lp_in, rv_lpl, rv_lpr, rv_yl, rv_yr;
#if REV_DIV == 2u
static int32_t rv_hold;
#endif
static uint32_t rev_pre_n = 1, dly_active, rev_active;
#if REV_DIV == 2u
static uint32_t fx_odd;
#endif

static void fx_init(void)
{
    line_init(&cho, CHO_N);
    line_init(&dl, DLY_MAX);
    line_init(&dr, DLY_MAX);
    line_init(&rv_pre, REV_PRE_N);
    line_init(&rv_in[0], RV_IN1); line_init(&rv_in[1], RV_IN2); line_init(&rv_in[2], RV_IN3); line_init(&rv_in[3], RV_IN4);
    line_init(&rv_ap1, RV_AP1 + 16u); line_init(&rv_d1, RV_D1); line_init(&rv_ap2, RV_AP2); line_init(&rv_d2, RV_D2);
    line_init(&rv_ap3, RV_AP3 + 16u); line_init(&rv_d3, RV_D3); line_init(&rv_ap4, RV_AP4); line_init(&rv_d4, RV_D4);
    dly_cur = dly_time = 11025 << 16;
}

/* the patch's numbers, once per ISR call. quarter: frames per quarter note (the arp's clock) */
static void fx_block(const patch_t *p, uint32_t quarter)
{
    int32_t d, t;
    uint32_t div = p->v[P_DLY_TIME] % NDDIV, samples;
    d = p->v[P_CHO_RATE]; cho_inc = (uint32_t)(10 + ((d * d * 590) >> 14)) * 974u;   /* 0.1 .. 5.9 Hz */
    cho_depth_k = p->v[P_CHO_DEPTH] * 67;       /* x the triangle's +-2048 = Q16 samples: up to +-6 ms */
    cho_mix = p->v[P_CHO_MIX];
    dly_mix = (p->v[P_DLY_MIX] * 180) >> 7;     /* wet up to 0.7 of the dry (Q8): the sum stays in range */
    rev_mix = (p->v[P_REV_MIX] * 180) >> 7;
    dly_fb = p->v[P_DLY_FB] * 250;              /* up to 0.97 */
    t = p->v[P_DLY_TONE]; dly_tone_k = clampi(((t * t) >> 6) + 8, 8, 256);
    d = p->v[P_REV_SIZE]; rev_decay = 12000 + ((d * d * 19500) >> 14);   /* 0.37 .. 0.96: the top is a near-freeze */
    d = p->v[P_REV_DAMP]; rev_damp_k = 32768 - d * d * 2;   /* the one-pole's k at the half rate */
    if (REV_DIV == 1u) {                        /* the same corner at the full rate: k' = 1 - sqrt(1 - k) */
        uint32_t x = (uint32_t)(32768 - rev_damp_k) << 15, r = 0, bit = 1u << 15;
        for (; bit; bit >>= 1)
            if ((r + bit) * (r + bit) <= x) r += bit;
        rev_damp_k = 32768 - (int32_t)r;
    }
    rev_pre_n = 1u + (uint32_t)p->v[P_REV_PRE] * (41u / REV_DIV);
    samples = quarter * DDIV_NUM[div] / DDIV_DEN[div];
    if (samples > DLY_MAX - 4u) samples = DLY_MAX - 4u;
    if (samples < 64u) samples = 64u;
    dly_time = (int32_t)(samples << 16);
    if (dly_mix) dly_active = 12u * FS; else if (dly_active > HALF_FRAMES) dly_active -= HALF_FRAMES; else dly_active = 0;
    if (rev_mix) rev_active = 24u * FS; else if (rev_active > HALF_FRAMES) rev_active -= HALF_FRAMES; else rev_active = 0;
}

static inline __attribute__((always_inline)) int32_t line_read_q16(const line_t *l, int32_t back_q16)
{
    uint32_t b = (uint32_t)back_q16 >> 16;
    int32_t f = (back_q16 >> 8) & 255, a = line_read(l, b), c = line_read(l, b + 1u);
    return a + (((c - a) * f) >> 8);
}
/* 4-point Hermite for a tap that is always moving (the chorus): no treble loss between the samples */
static inline __attribute__((always_inline)) int32_t line_read_q16_h(const line_t *l, int32_t back_q16)
{
    uint32_t b = (uint32_t)back_q16 >> 16;
    int32_t f = (back_q16 >> 8) & 255, p0 = line_read(l, b - 1u), p1 = line_read(l, b), p2 = line_read(l, b + 1u), p3 = line_read(l, b + 2u);
    int32_t c1 = (p2 - p0) >> 1, c2 = p0 - ((5 * p1) >> 1) + 2 * p2 - (p3 >> 1), c3 = ((p3 - p0) >> 1) + ((3 * (p1 - p2)) >> 1);
    return p1 + ((f * (c1 + ((f * (c2 + ((f * c3) >> 8))) >> 8))) >> 8);
}

/* one reverb sample at the half rate: in -> pre-delay -> bandwidth -> 4 diffusers -> the two tank halves */
static inline void rev_tick(int32_t in)
{
    int32_t x, a, b, e1 = tri_q(rv_ph1), e2 = tri_q(rv_ph2), dec = rev_decay, dk = rev_damp_k;
    rv_ph1 += 97391u * REV_DIV;                 /* ~1.0 Hz and ~0.7 Hz: the tank's sway against metallic rings */
    rv_ph2 += 68174u * REV_DIV;
    line_write(&rv_pre, in);
    x = line_read(&rv_pre, rev_pre_n);
    rv_lp_in += ((x - rv_lp_in) * RV_BW) >> 15;
    x = ap_step(&rv_in[0], rv_lp_in, 24576);
    x = ap_step(&rv_in[1], x, 24576);
    x = ap_step(&rv_in[2], x, 20480);
    x = ap_step(&rv_in[3], x, 20480);
    /* left half, fed by the right's end */
    a = x + ((line_read(&rv_d4, RV_D4) * dec) >> 15);
    a = ap_mod(&rv_ap1, a, -22938, RV_AP1, e1);
    line_write(&rv_d1, a);
    b = line_read(&rv_d1, RV_D1);
    rv_lpl += ((b - rv_lpl) * dk) >> 15;
    b = ap_step(&rv_ap2, (rv_lpl * dec) >> 15, 16384);
    line_write(&rv_d2, b);
    /* right half, fed by the left's end */
    a = x + ((line_read(&rv_d2, RV_D2) * dec) >> 15);
    a = ap_mod(&rv_ap3, a, -22938, RV_AP3, e2);
    line_write(&rv_d3, a);
    b = line_read(&rv_d3, RV_D3);
    rv_lpr += ((b - rv_lpr) * dk) >> 15;
    b = ap_step(&rv_ap4, (rv_lpr * dec) >> 15, 16384);
    line_write(&rv_d4, b);
    /* the output taps (the paper's), x 0.6 */
    a = line_read(&rv_d3, RVL(266)) + line_read(&rv_d3, RVL(2974)) - line_read(&rv_ap4, RVL(1913)) + line_read(&rv_d4, RVL(1996))
      - line_read(&rv_d1, RVL(1990)) - line_read(&rv_ap2, RVL(187)) - line_read(&rv_d2, RVL(1066));
    b = line_read(&rv_d1, RVL(353)) + line_read(&rv_d1, RVL(3627)) - line_read(&rv_ap2, RVL(1228)) + line_read(&rv_d2, RVL(2673))
      - line_read(&rv_d3, RVL(2111)) - line_read(&rv_ap4, RVL(335)) - line_read(&rv_d4, RVL(121));
    rv_yl = (a * 19661) >> 15;
    rv_yr = (b * 19661) >> 15;
}

/* the three effects side by side, each fed by its send bus; out gets their returns, MIX = the return's level.
 * DRIVE, the 4th send: the tracks' drive sends distorted, its return beside the dry sound (parallel: the dry keeps its
 * punch). Types OVERDRIVE (soft), DIST (harder, uneven), FUZZ (square), CRUSH (fewer bits, held samples); TONE a
 * low-pass after it, MIX the return. Fixed point (VIBE's is float): tanh by the softclip table */
enum { DV_OVERDRIVE, DV_DIST, DV_FUZZ, DV_CRUSH, NDV };
static volatile uint8_t ctl_dv_type, ctl_dv_amt = 60, ctl_dv_tone = 90, ctl_dv_mix = 127;
static int32_t dv_lp, dv_hold;                  /* Q15 */
static uint32_t dv_n;
static void drive_process(int32_t *out, const int32_t *vs, uint32_t n)
{
    uint32_t i, ty = ctl_dv_type % NDV, amt = ctl_dv_amt;
    int32_t pre = 256 + (int32_t)(24u * 256u * amt / 127u);   /* Q8: 1 .. 25 */
    int32_t k = 1311 + (int32_t)(31457u * ctl_dv_tone * ctl_dv_tone / 16129u);   /* Q15: 0.04 .. 1 */
    int32_t ret = (int32_t)(ctl_dv_mix * 4000u / 127u);   /* (MIX full: the return about as loud as the dry sound) */
    if (!ctl_dv_mix) return;
    for (i = 0; i < n; i++) {
        int32_t x = (vs[i] * pre) >> 9, y;      /* softclip(x) = tanh(the drive's x), Q15 */
        if (ty == DV_OVERDRIVE) y = softclip(x);
        else if (ty == DV_DIST) y = x > 0 ? softclip(x * 8 / 5) : (softclip(x * 7 / 10) * 4) / 5;
        else if (ty == DV_FUZZ) y = clampi(x * 40, -32767, 32767);   /* (20 x the drive's x, square beyond 1/20) */
        else {                                  /* CRUSH: 16 steps, a sample held for 2..12 */
            if (!dv_n--) { dv_n = 2u + amt * 10u / 127u; dv_hold = softclip(x) * 8 / 32768 * 4096; }
            y = dv_hold;
        }
        dv_lp += ((y - dv_lp) * k) >> 15;
        {
            int32_t v = (dv_lp * ret + 16384) >> 15;   /* (rounded: the one-pole's -1 it settles on stays silent) */
            out[2u * i] = sat16(out[2u * i] + v);
            out[2u * i + 1u] = sat16(out[2u * i + 1u] + v);
        }
    }
}

/* MASTER, the sum after the effects: DJ filter (one knob: below the middle low-pass, above high-pass), drive,
 * compressor, volume, and a soft limiter above -2 dB that never lets it clip. Fixed point: the compressor's gain in
 * log2 per control block (its level follower per sample), a line between blocks */
static volatile uint8_t ctl_m_vol = 100, ctl_m_dj = 64, ctl_m_drive, ctl_m_comp;   /* (main loop writes) volume (100 =
                                                 * unity), DJ filter (64 off, below LP, above HP), drive, compressor */
static volatile uint32_t ui_out_peak;           /* the output's peak, for the MASTER row's meter (the UI takes it) */
static svf_coef_t m_c;
static svf_state_t m_fl, m_fr;
static int32_t m_dj_was = 0x7FFF, m_env, m_g = 1 << 14;   /* m_env: the level follower Q23; m_g: the gain now, Q14 */

static int32_t log2_q16(uint32_t x)             /* x > 0 -> log2(x) in Q16 (+-0.01) */
{
    int32_t e = 31, f;
    if (!x) return -(32 << 16);
    while (!(x >> e)) e--;
    f = (int32_t)(e >= 16 ? x >> (e - 16) : x << (16 - e)) - 65536;   /* the mantissa - 1, Q16 */
    return (e << 16) + f + (int32_t)(((int64_t)f * (65536 - f) * 22713) >> 32);   /* log2(1+f) ~ f + 0.3466 f (1-f) */
}
static int32_t exp2_q14(int32_t l)              /* 2^(l / 65536) in Q14, l within +-14 octaves */
{
    int32_t e = l >> 16, f = l & 0xFFFF, m;
    m = 16384 + ((f * 10756) >> 16) + (int32_t)(((int64_t)f * f * 5628) >> 32);   /* 2^f ~ 1 + 0.6565 f + 0.3435 f^2 */
    return e >= 0 ? (e > 14 ? 0x7FFFFFFF : m << e) : (e < -14 ? 0 : m >> -e);
}

static void master_process(int32_t *out, uint32_t n)
{
    int32_t dj = (int32_t)ctl_m_dj - 64, mode = dj <= -2 ? 1 : dj >= 2 ? 2 : 0, ad = dj < 0 ? -dj : dj;
    uint32_t d = ctl_m_drive, cm = ctl_m_comp, i, b, pk = 0;
    int32_t pre = 256 + (int32_t)(8u * 256u * d / 127u), post = (int32_t)(4096u * (127u + d) * 256u / (127u * (uint32_t)pre));
    int32_t vol = (int32_t)(ctl_m_vol * ctl_m_vol * 4096u / 10000u);   /* Q12, 100 = unity, the square law */
    if (dj != m_dj_was) {                       /* LP 18 kHz .. 60 Hz, HP 20 Hz .. 6 kHz, Q 1.1 */
        svf_coef(&m_c, mode == 1 ? 34099 - ad * 395 : 3968 + ad * 395, 3724);
        if (!mode || (m_dj_was <= -2) != (dj <= -2)) m_fl.ic1 = m_fl.ic2 = m_fr.ic1 = m_fr.ic2 = 0;
        m_dj_was = dj;
    }
    for (b = 0; b < n; b += CTL) {
        int32_t g0 = m_g, g1 = 1 << 14;
        if (cm) {                               /* the gain at this block's end: from the level so far */
            int32_t ex = (int32_t)(65536u * 5u * cm / (127u + 5u * cm));   /* Q16: the ratio's 1 - 1/r, 0 .. 0.83 */
            int32_t thr = -(int32_t)((508u + 20u * cm) * 857u / 10u);      /* log2 Q16 of -4 .. -24 dB */
            int32_t lv = log2_q16((uint32_t)(m_env >> 8) + 1u) - (15 << 16);   /* the level, log2 of full scale */
            int32_t gl = (int32_t)(((int64_t)(-thr) * ex * 29491) >> 32);  /* make-up: (1/thr)^(0.45 ex) */
            if (lv > thr) gl -= (int32_t)(((int64_t)(lv - thr) * ex) >> 16);
            g1 = exp2_q14(gl);
        }
        m_g = g1;
        for (i = b; i < b + CTL && i < n; i++) {
            int32_t l = out[2u * i], r = out[2u * i + 1u], g = g0 + (((g1 - g0) * (int32_t)(i - b)) >> 4);
            uint32_t al, ar;
            if (mode && !(l | r) && (uint32_t)(m_fl.ic1 + 8) < 16u && (uint32_t)(m_fl.ic2 + 8) < 16u
                && (uint32_t)(m_fr.ic1 + 8) < 16u && (uint32_t)(m_fr.ic2 + 8) < 16u)
                m_fl.ic1 = m_fl.ic2 = m_fr.ic1 = m_fr.ic2 = 0;   /* (silence stays silence: no rounding hum of the states) */
            else if (mode) {                    /* (at half the level: the filter's band-pass state stays linear) */
                int32_t lp, bp, hp;
                svf_step(&m_c, &m_fl, l >> 1, &lp, &bp, &hp); l = (mode == 1 ? lp : hp) << 1;
                svf_step(&m_c, &m_fr, r >> 1, &lp, &bp, &hp); r = (mode == 1 ? lp : hp) << 1;
            }
            if (d) { l = (softclip((l * pre) >> 9) * post) >> 12; r = (softclip((r * pre) >> 9) * post) >> 12; }
            if (cm) {                           /* the level follower: 3 ms up, 150 ms down */
                int32_t p = (l < 0 ? -l : l) > (r < 0 ? -r : r) ? (l < 0 ? -l : l) : (r < 0 ? -r : r);
                int32_t e = clampi(p, 0, 65535) << 8;
                m_env += e > m_env ? (int32_t)(((int64_t)(e - m_env) * 495) >> 16) : (int32_t)(((int64_t)(e - m_env) * 10) >> 16);
                l = (int32_t)(((int64_t)l * g) >> 14); r = (int32_t)(((int64_t)r * g) >> 14);
            }
            l = (l * vol) >> 12; r = (r * vol) >> 12;
            al = (uint32_t)(l < 0 ? -l : l); ar = (uint32_t)(r < 0 ? -r : r);
            if (al > 26214u) { int32_t y = 26214 + softclip(((int32_t)al - 26214) * 5 / 2) / 5; l = l < 0 ? -y : y; }
            if (ar > 26214u) { int32_t y = 26214 + softclip(((int32_t)ar - 26214) * 5 / 2) / 5; r = r < 0 ? -y : y; }
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
            out[2u * i] = l; out[2u * i + 1u] = r;
        }
    }
    if (pk > ui_out_peak) ui_out_peak = pk;
}

static void fx_process(int32_t *out, const int32_t *cs, const int32_t *ds, const int32_t *rs, uint32_t n)
{
    uint32_t i, do_dly = dly_active != 0, do_rev = rev_active != 0;
    int32_t cm = cho_mix, dm = dly_mix, rm = rev_mix;
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (cm) {                               /* two taps a quarter turn apart on one line, around 12 ms */
            int32_t bl = (529 << 16) + tri_q(cho_ph) * cho_depth_k, br = (529 << 16) + tri_q(cho_ph + 0x40000000u) * cho_depth_k;
            cho_ph += cho_inc;
            line_write(&cho, cs[i]);
            l += (line_read_q16_h(&cho, bl) * cm) >> 8;
            r += (line_read_q16_h(&cho, br) * cm) >> 8;
        }
        if (do_dly) {                           /* ping-pong: the input enters left, the feedback crosses */
            int32_t wl, wr;
            if (dly_cur < dly_time) { dly_cur += 1 << 15; if (dly_cur > dly_time) dly_cur = dly_time; }
            else if (dly_cur > dly_time) { dly_cur -= 1 << 15; if (dly_cur < dly_time) dly_cur = dly_time; }
            wl = line_read_q16(&dl, dly_cur);
            wr = line_read_q16(&dr, dly_cur);
            dly_lpl += ((wr - dly_lpl) * dly_tone_k) >> 8;
            dly_lpr += ((wl - dly_lpr) * dly_tone_k) >> 8;
            line_write(&dl, ds[i] + ((dly_lpl * dly_fb) >> 15));
            line_write(&dr, (dly_lpr * dly_fb) >> 15);
            l += (wl * dm) >> 8;
            r += (wr * dm) >> 8;
        }
        if (do_rev) {
#if REV_DIV == 2u                               /* half rate: a pair in, the output interpolated between ticks */
            int32_t pl = rv_yl, pr = rv_yr, din = rs[i] * 2;
            if (!(fx_odd ^= 1u)) {
                rev_tick((rv_hold + din) >> 2);
                l += (((pl + rv_yl) >> 1) * rm) >> 8;
                r += (((pr + rv_yr) >> 1) * rm) >> 8;
            } else {
                l += (rv_yl * rm) >> 8;
                r += (rv_yr * rm) >> 8;
            }
            rv_hold = din;
#else
            rev_tick(rs[i]);
            l += (rv_yl * rm) >> 8;
            r += (rv_yr * rm) >> 8;
#endif
        }
        out[2u * i] = sat16(l);
        out[2u * i + 1u] = sat16(r);
    }
}
