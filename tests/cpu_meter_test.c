/* SPDX-License-Identifier: GPL-3.0-only */
/* The GLOBAL page's CPU meter (firmware/src/cpu_meter.h): a slow half is held ~2 s, then the level falls back; the
 * halves that took longer than a half lasts are counted. */
#include <stdio.h>
#include "../firmware/src/cpu_meter.h"

static int fails;
static void check(int ok, const char *what) { printf("cpu_meter: %-60s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    cpu_meter_t m = {0, 0, 0};
    uint32_t t;
    for (t = 0; t < 1000u; t += 6u) cpu_meter_add(&m, 1500, t);
    check(m.peak == 1500 && m.over == 0, "steady 1500 us: shows 1500, nothing late");
    cpu_meter_add(&m, 4800, 1000);               /* one slow half (a burst of note-ons) */
    for (t = 1006; t < 2900u; t += 6u) cpu_meter_add(&m, 1500, t);
    check(m.peak == 4800, "one slow half among fast ones: still shown 1.9 s later");
    for (; t < 3100u; t += 6u) cpu_meter_add(&m, 1500, t);
    check(m.peak == 1500, "... and gone after the 2 s hold");
    cpu_meter_add(&m, 6100, t);
    cpu_meter_add(&m, 5900, t + 6u);
    cpu_meter_add(&m, 5700, t + 12u);
    check(m.over == 2 && m.peak == 6100, "halves over 5805 us counted (2), the worst held");
    m.over = 9999u;
    cpu_meter_add(&m, 7000, t + 18u);
    check(m.over == 9999u, "the count stops at 9999 (the page's width)");
    {   /* fm1_ms wraps after 49 days: the hold still ends */
        cpu_meter_t w = {3000, 0xFFFFFF00u, 0};
        cpu_meter_add(&w, 1000, 0x00000800u);
        check(w.peak == 1000, "the millisecond counter wrapping: the hold still ends");
    }
    printf(fails ? "cpu_meter: FAILED\n" : "cpu_meter: all ok\n");
    return fails != 0;
}
