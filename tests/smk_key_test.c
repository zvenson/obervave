/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* SMK-37 keybed (hal/smk_input.h): the velocity from the time between the two contacts; a slow press plays once
 * SMK_KEY_TMAX_MS has passed with contact 1 alone, and its contact 2 plays nothing more; a fast strike as before.
 *   cc -O1 -Ifirmware/hal -o build/host/smk_key_test tests/smk_key_test.c && build/host/smk_key_test */
#include <stdio.h>
#include "smk_input.h"

static uint32_t now, ev_n, ev_vel;
static void set_contacts(uint32_t k, uint32_t c1, uint32_t c2)
{
    uint32_t i, c[2] = {c1, c2};
    for (i = 0; i < 2u; i++) {
        uint32_t code = SMK_KEYC[k][i];
        if (c[i]) fm1_in.raw[code >> 4] |= (uint16_t)(1u << (code & 15u));
        else fm1_in.raw[code >> 4] &= (uint16_t)~(1u << (code & 15u));
    }
}
static void frames(uint32_t ms)                 /* a frame every 1.6 ms, as the scan */
{
    uint32_t end = now + ms * 1000u * FM1_TICKS_PER_US, before;
    while (now < end) {
        before = fm1_in.notes;
        smk__keys(now);
        if ((fm1_in.notes & ~before) & 1u) { ev_n++; ev_vel = fm1_in.key_vel[SMK_KEY_F3]; }
        now += 1600u * FM1_TICKS_PER_US;
    }
}

int main(void)
{
    int fail = 0;
    uint32_t k = SMK_KEY_F3;                     /* F3: note bit 0 */
    smk_trim_defaults();
    /* a fast strike: contact 2 2 ms after contact 1 -> a loud note at once */
    set_contacts(k, 1, 0); frames(2); set_contacts(k, 1, 1); frames(10);
    if (ev_n != 1 || ev_vel < 120) { printf("FAIL fast strike: %u notes, velocity %u\n", ev_n, ev_vel); fail = 1; }
    set_contacts(k, 0, 0); frames(20);
    if (fm1_in.notes & 1u) { printf("FAIL fast strike: not released\n"); fail = 1; }
    /* a medium strike (15 ms) -> one note, velocity in between */
    ev_n = 0; set_contacts(k, 1, 0); frames(15); set_contacts(k, 1, 1); frames(10);
    if (ev_n != 1 || ev_vel < 60 || ev_vel > 100) { printf("FAIL medium strike: %u notes, velocity %u\n", ev_n, ev_vel); fail = 1; }
    set_contacts(k, 0, 0); frames(20);
    /* a slow press: contact 1 alone for 100 ms -> the note after SMK_KEY_TMAX_MS, soft, once */
    ev_n = 0; set_contacts(k, 1, 0); frames(SMK_KEY_TMAX_MS - 3u);
    if (ev_n) { printf("FAIL slow press: a note before %u ms\n", SMK_KEY_TMAX_MS); fail = 1; }
    frames(5);
    if (ev_n != 1 || ev_vel > 40) { printf("FAIL slow press: %u notes by %u ms, velocity %u\n", ev_n, SMK_KEY_TMAX_MS + 2u, ev_vel); fail = 1; }
    frames(60); set_contacts(k, 1, 1); frames(10);
    if (ev_n != 1) { printf("FAIL slow press: contact 2 played again (%u notes)\n", ev_n); fail = 1; }
    set_contacts(k, 1, 0); frames(10);
    if (!(fm1_in.notes & 1u)) { printf("FAIL slow press: released on contact 2 opening\n"); fail = 1; }
    set_contacts(k, 0, 0); frames(20);
    if (fm1_in.notes & 1u) { printf("FAIL slow press: not released\n"); fail = 1; }
    printf(fail ? "SMK keys: FAILED\n" : "SMK keys: fast, medium and slow presses ok (slow: velocity %u after %u ms)\n", ev_vel, SMK_KEY_TMAX_MS);
    return fail;
}
