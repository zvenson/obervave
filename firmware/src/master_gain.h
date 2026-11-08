/* SPDX-License-Identifier: GPL-3.0-only */
/* The MASTER level on an audio half (stereo, interleaved): the gain glides from the last half's to this one's,
 * sample by sample, so a knob turned while a note sounds does not step the level every 5.8 ms (a click a step; the
 * SMK-37's K1 moves ~0.6 dB a detent). Q12 gains (4096 = unity), the output clamped to +-32767 as before. With the
 * same gain on both ends it is exactly the old (o * gain) >> 12. tests/master_gain_test.c */
#pragma once
#include <stdint.h>

static void master_gain_apply(int32_t *o, uint32_t frames, int32_t from_q12, int32_t to_q12)
{
    int32_t g16 = from_q12 * 65536, dg = frames ? (to_q12 - from_q12) * 65536 / (int32_t)frames : 0;   /* (no shifts of negatives) */
    uint32_t i;
    for (i = 0; i < frames; i++) {
        int32_t g = g16 >> 16, l = (o[2u * i] * g) >> 12, r = (o[2u * i + 1u] * g) >> 12;
        o[2u * i] = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        o[2u * i + 1u] = r > 32767 ? 32767 : r < -32767 ? -32767 : r;
        g16 += dg;
    }
}
