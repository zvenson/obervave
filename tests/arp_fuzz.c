/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Stuck notes: random keys (as play_note sends them), the arp's modes, sustain / HOLD, voice modes and track B's
 * own sound coming and going; then every key up, the pedal and HOLD let go: after 2 s nothing may still hold a note.
 *   cc -O1 -DOBV_HOST [-DBOARD_SMK37=1] -Ifirmware/src -Ifirmware/gen -o build/host/arp_fuzz tests/arp_fuzz.c */
#define main obv_host_main
#include "../firmware/src/obervave.c"
#undef main

static uint32_t fz = 12345u;
static uint32_t fz_rnd(uint32_t n) { fz ^= fz << 13; fz ^= fz >> 17; fz ^= fz << 5; return fz % n; }
static char lg[400][96]; static uint32_t lgn, want = 0xFFFFFFFFu;
#define LOG(...) do { if (lgn < 400u) snprintf(lg[lgn++], 96, __VA_ARGS__); } while (0)
static int32_t buf[2u * HALF_FRAMES];
static uint32_t trace;
static void check(void)
{
    uint32_t w, q, ok;
    if (want == 0xFFFFFFFFu) return;
    if (trace) LOG("   c0 g%u p%u n%u | arp1 snd%u n%u | held1 %u [%u %u] phys %u | B mode %u", vc[0].gate, vc[0].part, vc[0].note, arp[1].sounding, arp[1].note, nheld[1], held[1][0].note, held[1][1].note, held[1][0].phys, live_b.v[P_MODE]);
    for (w = 0; w < NV; w++) {
        uint32_t pp = vc[w].part;
        if (!vc[w].gate) continue;
        ok = pt[pp].p->v[P_ARP_MODE] != A_OFF ? arp[pp].sounding && arp[pp].note == vc[w].note : 0u;
        for (q = 0; q < nheld[pp] && !ok; q++) ok = held[pp][q].note == vc[w].note;
        if (!ok) { LOG("!! card %u part %u note %u gated, nothing holds it (b_own %u, arp %u/%u snd %u/%u)", w, pp, vc[w].note, b_own, live.v[P_ARP_MODE], live_b.v[P_ARP_MODE], arp[0].sounding, arp[1].sounding); return; }
    }
}
static void blocks(uint32_t n) { while (n--) { uint32_t i; for (i = 0; i < 2u * HALF_FRAMES; i++) buf[i] = 0; app_audio(buf, HALF_FRAMES); check(); } }

int main(int argc, char **argv)
{
    uint32_t run, runs = argc > 1 ? (uint32_t)atoi(argv[1]) : 2000u, bad = 0, b_toggle = argc > 2 ? (uint32_t)atoi(argv[2]) : 1u;
    if (argc > 3) want = (uint32_t)atoi(argv[3]);
    engine_init();
    cc_map_init();
    set.tempo = 120; ctl_tempo = 120;
    load_pattern(0);
    for (run = 0; run < runs; run++) {
        uint8_t down[128] = {0};
        uint32_t step, n, v;
        if (want != 0xFFFFFFFFu && run != want) continue;
        fz = 0x9E3779B9u * (run + 1u); lgn = 0;
        if (want != 0xFFFFFFFFu && run != want) continue;
        load_program(fz_rnd(NPROG));
        { patch_t p; prog_get(fz_rnd(NPROG), &p); live_b = p; }
        pat.trk[1].B_PROG = b_toggle == 2u;
        ctl_sustain = ctl_hold = 0;
        ctl_panic = 1; blocks(4);
        for (step = 0; step < 400u; step++) {
            uint32_t r = fz_rnd(100);
            n = 60u + fz_rnd(8);
            if (r < 40) {                           /* a key */
                if (!down[n]) { down[n] = 1; note_part[n] = (uint8_t)fz_rnd(2); note_event_p(note_part[n], 1, n, 1u + fz_rnd(127)); LOG("on %u p%u", n, note_part[n]); }
                else { down[n] = 0; note_event_p(note_part[n], 0, n, 0); LOG("off %u p%u", n, note_part[n]); }
            } else if (r < 47) { ctl_sustain = !ctl_sustain; LOG("sus %u", ctl_sustain); }
            else if (r < 52) { ctl_hold = !ctl_hold; LOG("hold %u", ctl_hold); }
            else if (r < 60) { live.v[P_ARP_MODE] = (uint8_t)fz_rnd(PDEF[P_ARP_MODE].max + 1u); LOG("A arp %u", live.v[P_ARP_MODE]); }
            else if (r < 64) { live_b.v[P_ARP_MODE] = (uint8_t)fz_rnd(PDEF[P_ARP_MODE].max + 1u); LOG("B arp %u", live_b.v[P_ARP_MODE]); }
            else if (r < 68) { live.v[P_MODE] = (uint8_t)fz_rnd(PDEF[P_MODE].max + 1u); LOG("A mode %u", live.v[P_MODE]); }
            else if (r < 70) { live_b.v[P_MODE] = (uint8_t)fz_rnd(PDEF[P_MODE].max + 1u); LOG("B mode %u", live_b.v[P_MODE]); }
            else if (r < 72) live.v[P_ARP_GATE] = (uint8_t)fz_rnd(128);
            else if (r < 75 && b_toggle == 1u) pat.trk[1].B_PROG = pat.trk[1].B_PROG ? 0 : 1;
            else if (r < 78) { load_program(fz_rnd(NPROG)); LOG("program %u", prog); }
            else if (r < 80) { patch_t pb; prog_get(fz_rnd(NPROG), &pb); live_b = pb; LOG("B program"); }
            if (want != 0xFFFFFFFFu && step >= 395u) trace = 1;
            blocks(1u + fz_rnd(6));
        }
        for (n = 0; n < 128u; n++) if (down[n]) note_event_p(note_part[n], 0, n, 0);
        LOG("-- all keys up"); blocks(2); ctl_sustain = 0; ctl_hold = 0; LOG("-- pedal, HOLD up");
        blocks(3); trace = 0; blocks(397);
        for (v = 0; v < NV; v++)
            if (vc[v].gate) break;
        if (v < NV || nheld[0] || nheld[1] || arp[0].sounding || arp[1].sounding) {
            if (bad++ < 5)
                printf("run %u: stuck: voice %d gate (note %u part %u), nheld %u/%u, arp sounding %u/%u, arp mode %u/%u, B %u\n",
                       run, v < NV ? (int)v : -1, v < NV ? vc[v].note : 0u, v < NV ? vc[v].part : 0u, nheld[0], nheld[1],
                       arp[0].sounding, arp[1].sounding, live.v[P_ARP_MODE], live_b.v[P_ARP_MODE], pat.trk[1].B_PROG);
        }
    }
    if (want != 0xFFFFFFFFu) { uint32_t i; for (i = 0; i < lgn; i++) printf("  %s\n", lg[i]); }
    printf("%u of %u runs left a note held\n", bad, runs);
    return bad != 0;
}
