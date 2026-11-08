/* SPDX-License-Identifier: GPL-3.0-only */
/* The MASTER gain stage (firmware/src/master_gain.h): unchanged at a steady level, a glide (no step) when the level
 * moves, the clamp, and that it removes the click a knob step made (the high-frequency energy of a held low note). */
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/src/master_gain.h"

#define N 256u
static int fails;
static void check(int ok, const char *what) { printf("master_gain: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static int32_t old_gain(int32_t v, int32_t m)   /* the stage before: one gain for the whole half */
{
    v = (v * m) >> 12;
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}
static int32_t tone(uint32_t i) { return (int32_t)((i * 37u) % 400u) * 60 - 12000; }   /* a low saw, ~110 Hz */

int main(void)
{
    static int32_t a[2 * N], b[2 * N];
    uint32_t i;
    int same = 1, mono = 1, ends = 1;

    for (i = 0; i < 2 * N; i++) a[i] = b[i] = tone(i / 2u) * (i & 1u ? -1 : 1);
    master_gain_apply(a, N, 2047, 2047);
    for (i = 0; i < 2 * N; i++) same &= a[i] == old_gain(b[i], 2047);
    check(same, "steady level: exactly the old (o * gain) >> 12");

    {   /* a constant input through a rising gain: the output rises in small steps, never back, ends at the target */
        int32_t c[2 * N], prev = 0, maxstep = 0;
        for (i = 0; i < 2 * N; i++) c[i] = 10000;
        master_gain_apply(c, N, 2047, 2185);     /* one K1 detent at the default (724 -> 748) */
        for (i = 0; i < N; i++) {
            if (i && c[2 * i] < prev) mono = 0;
            if (i && c[2 * i] - prev > maxstep) maxstep = c[2 * i] - prev;
            prev = c[2 * i];
            if (c[2 * i] != c[2 * i + 1]) mono = 0;
        }
        ends = c[0] == old_gain(10000, 2047) && abs(c[2 * (N - 1)] - old_gain(10000, 2185)) <= 2;
        check(mono, "a rising level: the output rises steadily, both channels alike");
        check(maxstep <= 3, "... in steps of at most 3, one Q12 unit (a step at once was 337)");
        check(ends, "... from the old level to the new one within the half");
    }
    {   /* falling, to silence */
        int32_t c[2 * N];
        for (i = 0; i < 2 * N; i++) c[i] = -20000;
        master_gain_apply(c, N, 4088, 0);
        check(c[0] == old_gain(-20000, 4088) && c[2 * (N - 1)] > -100 && c[2 * (N - 1)] <= 0, "a falling level down to 0: glides to silence");
    }
    {   /* the clamp */
        int32_t c[4] = {40000, -40000, 40000, -40000};
        master_gain_apply(c, 2, 4096, 4096);
        check(c[0] == 32767 && c[1] == -32767, "beyond full scale: clamped to +-32767 as before");
    }
    {   /* the click itself: a held note, the level stepped half by half vs glided; energy of the second difference */
        static int32_t s[2 * N * 16], g[2 * N * 16];
        long long es = 0, eg = 0;
        int32_t m = 2047, last = 2047;
        uint32_t h;
        for (i = 0; i < 2 * N * 16; i++) s[i] = g[i] = tone(i / 2u);
        for (h = 0; h < 16u; h++) {
            int32_t *ps = s + 2 * N * h, *pg = g + 2 * N * h;
            if (h % 2u == 1u) m += 140;           /* a detent every second half: K1 turned fast */
            for (i = 0; i < 2 * N; i++) ps[i] = old_gain(ps[i], m);
            master_gain_apply(pg, N, last, m);
            last = m;
        }
        for (i = 4; i < 2 * N * 16; i += 2) {    /* (the saw's own corners are in both: only the excess differs) */
            int32_t ds = s[i] - 2 * s[i - 2] + s[i - 4], dg = g[i] - 2 * g[i - 2] + g[i - 4];
            if (abs(ds) < 4000) es += (long long)ds * ds;
            if (abs(dg) < 4000) eg += (long long)dg * dg;
        }
        printf("master_gain: click energy (2nd difference, off the saw's corners): stepped %lld, glided %lld\n", es, eg);
        check(eg * 4 < es, "a knob turned during a note: the glide removes the clicks");
    }
    printf(fails ? "master_gain: FAILED\n" : "master_gain: all ok\n");
    return fails != 0;
}
