/* SPDX-License-Identifier: GPL-3.0-only */
/* The render time of the audio halves, for the GLOBAL page: the highest of the last ~2 s (one slow half between
 * fast ones is what a crackle on a note-on would be; the last half alone hides it) and how many halves took longer
 * than the half itself lasts (5805 us: the I2S played on before the half was ready). Written in the audio ISR, read by
 * the main loop. tests/cpu_meter_test.c */
#pragma once
#include <stdint.h>

#define CPU_HALF_US 5805u                       /* 256 frames at 44.1 kHz */
#define CPU_HOLD_MS 2000u

typedef struct { volatile uint32_t peak, peak_t, over; } cpu_meter_t;

static void cpu_meter_add(cpu_meter_t *m, uint32_t us, uint32_t now_ms)
{
    if (us >= m->peak || now_ms - m->peak_t >= CPU_HOLD_MS) { m->peak = us; m->peak_t = now_ms; }
    if (us >= CPU_HALF_US && m->over < 9999u) m->over++;
}
