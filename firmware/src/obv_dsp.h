/* SPDX-License-Identifier: GPL-3.0-only */
/* OberVave DSP building blocks, all fixed point (no FPU, no 64-bit division on the chip).
 * Oscillators: naive saw / pulse with polyBLEP corrections applied as discontinuity events, one sample late:
 * a jump of height H at fraction t before the current sample (t = 0..32767 ~ 0..1 sample) adds
 * H/2 * t^2 to the previous sample and takes H/2 * (1-t)^2 from the current one. A natural wrap and a
 * hard-sync reset are the same event, so one piece of code serves both (as the plan in CLAUDE.md). */
#include "obv_tables.h"

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* tanh(x / 32768) * 32767, interpolated (unity slope at 0): x = +-32768 -> 0.76 full scale, +-65536 -> 0.96 */
static inline __attribute__((always_inline)) int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* a soft knee: linear to +-32768, tanh above, +-65535 at most: the rails of the mixer and the filter (nothing in a
 * voice clips hard; the originals' op-amps round off too) */
static inline __attribute__((always_inline)) int32_t sat_knee(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 32768) return x;
    a = 32768 + softclip(a - 32768);
    return x < 0 ? -a : a;
}

/* tanh(x / 98304) * 98301: the filter's input stage (unity below ~30000, ~-0.6 dB at a two-saw chord) */
static inline __attribute__((always_inline)) int32_t sat_in(int32_t x) { return softclip((x * 85) >> 8) * 3; }

/* tanh(x / 65536) * 65534: the ladder's loop */
static inline __attribute__((always_inline)) int32_t sat_tanh(int32_t x) { return softclip(x >> 1) << 1; }

static inline uint32_t xorshift(uint32_t *st)
{
    uint32_t s = *st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = s;
    return s;
}

/* Q8 semitones (0 = MIDI 0, 8.18 Hz) -> phase step per frame; 12 octave values x 256 fine steps */
static inline uint32_t pitch_inc(int32_t p)
{
    uint32_t n, o;
    p = clampi(p, 0, 131 * 256 + 255);
    n = (uint32_t)p >> 8;
    o = n / 12u;
    return (uint32_t)(((uint64_t)INC_C9[n - o * 12u] * FINE_Q16[p & 255]) >> (26u - o));
}

/* ---- oscillators */
typedef struct {
    uint32_t ph;                                /* phase, wraps */
    int32_t prev;                               /* the previous sample, still open to a correction */
} osc_t;

#define OSC_SAW 1u
#define OSC_PLS 2u

static inline __attribute__((always_inline)) int32_t osc_naive(uint32_t ph, uint32_t pw, uint32_t wave)   /* -98304 .. 98303 */
{
    int32_t v = 0;
    if (wave & OSC_SAW)
        v = (int32_t)(ph >> 16) - 32768;
    if (wave & OSC_PLS)                         /* saw(ph) - saw(ph + pw): a pulse with mean 0 */
        v += (int32_t)(ph >> 16) - (int32_t)((ph + pw) >> 16);
    return v;
}

/* the event: a jump of H (any size up to +-200000) at t (0..32767) before the current sample */
static inline __attribute__((always_inline)) void osc_event(int32_t *prev, int32_t *cur, int32_t h, int32_t t)
{
    int32_t u = 32767 - t, h16 = h >> 4;
    *prev += (h16 * ((t * t) >> 15)) >> 12;   /* H/2 * t^2: h16 = H/16, t^2 in Q15, so >> 12 */
    *cur -= (h16 * ((u * u) >> 15)) >> 12;
}

/* one sample: advance by inc (d = inc >> 15, hw = the height of a wrap of ph: -65536 per saw component,
 * both precomputed per block), return the delayed output; o->prev then holds this sample's naive value (the
 * X-MOD source). reset_t >= 0: a hard-sync reset to phase 0 happened reset_t before the current sample
 * (0..32767 = 0..1 sample). Inlined by force: it is the inner loop. */
static inline __attribute__((always_inline)) int32_t osc_step(osc_t *o, uint32_t inc, uint32_t d, uint32_t pw,
                                                              uint32_t wave, int32_t hw, int32_t reset_t)
{
    uint32_t old = o->ph, ph = old + inc;
    int32_t cur, out = o->prev;
    if (reset_t < 0 || !d) {
        cur = osc_naive(ph, pw, wave);
        if (ph < inc && d)                      /* wrapped: the saw(s) drop */
            osc_event(&out, &cur, hw, (int32_t)(ph / d));
        if ((wave & OSC_PLS) && ph + pw < inc && d)   /* the subtracted saw wraps: the pulse rises */
            osc_event(&out, &cur, 65536, (int32_t)((ph + pw) / d));
    } else {
        uint32_t at = ph - d * (uint32_t)reset_t;   /* the phase at the reset instant */
        int32_t t;
        ph = d * (uint32_t)reset_t;             /* reset, then reset_t on */
        cur = osc_naive(ph, pw, wave);
        if (at < old) {                         /* ph wrapped on the way to the reset */
            t = (int32_t)(at / d) + reset_t;
            osc_event(&out, &cur, hw, t > 32767 ? 32767 : t);
        }
        if ((wave & OSC_PLS) && at + pw < old + pw) {   /* ph + pw wrapped on the way to the reset */
            t = (int32_t)((at + pw) / d) + reset_t;
            osc_event(&out, &cur, 65536, t > 32767 ? 32767 : t);
        }
        osc_event(&out, &cur, osc_naive(0, pw, wave) - osc_naive(at, pw, wave), reset_t);
    }
    o->ph = ph;
    o->prev = cur;
    return out;
}

/* ---- trapezoidal state-variable filter (A. Simper), coefficients per block in Q13 */
typedef struct { int32_t a1, a2, a3, k; } svf_coef_t;
typedef struct { int32_t ic1, ic2; } svf_state_t;

/* cut: Q8 semitones (0..136 << 8); k: damping Q12 (2.0 = 8192; ~0 self-oscillates) */
static inline void svf_coef(svf_coef_t *c, int32_t cut, int32_t k)
{
    int32_t i, g, den;
    cut = clampi(cut, 0, 136 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 136)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    if (den < 64) den = 64;
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}

/* one sample; *lp, *bp, *hp out. The band-pass state passes a soft saturation: resonance stays warm and bounded
 * (self-oscillation settles instead of running away). Signals within +-150000 keep the products in 32 bits. */
static inline __attribute__((always_inline)) void svf_step(const svf_coef_t *c, svf_state_t *s, int32_t in, int32_t *lp, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - s->ic2;
    int32_t v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 13;
    int32_t v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 13);
    int32_t n1 = 2 * v1 - s->ic1;
    s->ic1 = (uint32_t)(n1 + 24576) < 49152u ? n1 : softclip(n1 >> 1) << 1;   /* linear below +-0.75, then soft */
    s->ic2 = 2 * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = in - ((c->k * v1) >> 12) - v2;
}

static inline __attribute__((always_inline)) int32_t svf_lp(const svf_coef_t *c, svf_state_t *s, int32_t in)   /* the plain stage (24 dB's second) */
{
    int32_t v3 = in - s->ic2;
    int32_t v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 13;
    int32_t v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 13);
    s->ic1 = 2 * v1 - s->ic1;
    s->ic2 = 2 * v2 - s->ic2;
    return v2;
}

/* ---- the 24 dB ladder: four trapezoidal one-poles with the resonance fed back around all four (as the CEM3320 of the
 * later eight-voices), a tanh in the loop so the resonance compresses and self-oscillation stays round, the input
 * raised with the resonance (1 + k/2) so the bass does not fall away. The feedback is solved for the linear loop
 * (zero delay), the tanh then sits on the first stage's input: the usual cheap and stable approximation. */
typedef struct { int32_t G, G2, G3, G4, k, inv, gin; } lad_coef_t;   /* G.. Q12, k Q10 (4.0 = 4096 self-oscillates), inv gin Q12 */
typedef struct { int32_t s[4]; } lad_state_t;

static inline void lad_coef(lad_coef_t *c, int32_t cut, int32_t k10)
{
    int32_t i, g, G;
    cut = clampi(cut, 0, 136 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 136)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    G = (int32_t)(((uint32_t)g << 12) / (uint32_t)(4096 + g));
    c->G = G;
    c->G2 = (G * G) >> 12;
    c->G3 = (c->G2 * G) >> 12;
    c->G4 = (c->G3 * G) >> 12;
    c->k = k10;
    c->inv = (int32_t)((4096u << 12) / (uint32_t)(4096 + ((k10 * c->G4) >> 10)));
    c->gin = 4096 + 2 * k10;                   /* 1 + k/2: the bass stays, the loop is pushed harder as the resonance rises */
}

static inline __attribute__((always_inline)) int32_t lad_step(const lad_coef_t *c, lad_state_t *s, int32_t in)
{
    int32_t G = c->G, *st = s->s, v, y, u;
    int32_t S1 = st[0] - ((G * st[0]) >> 12), S2 = st[1] - ((G * st[1]) >> 12);
    int32_t S3 = st[2] - ((G * st[2]) >> 12), S4 = st[3] - ((G * st[3]) >> 12);
    int32_t x = (in * c->gin) >> 12;
    int32_t acc = ((c->G3 * S1 + c->G2 * S2 + G * S3) >> 12) + S4;
    int32_t y4 = (int32_t)(((int64_t)(((c->G4 * x) >> 12) + acc) * c->inv) >> 12);   /* the loop's output, linear */
    u = sat_tanh(x - ((c->k * clampi(y4, -131072, 131071)) >> 10));
    v = ((u - st[0]) * G) >> 12; y = v + st[0]; st[0] = y + v;
    v = ((y - st[1]) * G) >> 12; y = v + st[1]; st[1] = y + v;
    v = ((y - st[2]) * G) >> 12; y = v + st[2]; st[2] = y + v;
    v = ((y - st[3]) * G) >> 12; y = v + st[3]; st[3] = y + v;
    return y;
}
