/* SPDX-License-Identifier: GPL-3.0-only */
/* OberVave in a browser: the firmware's own sources (firmware/src/obervave.c, unchanged) on the host build's fake
 * FM-1 (tests/host.c: screen, settings, patterns, MIDI queue, buttons, LEDs), compiled to WebAssembly by
 * web/sim/build.sh (zig cc, wasm32-freestanding). The pattern follows Makkuro37's web/sim (M. Geisler, GPL-3.0) and
 * winampFM's simulator (hericdk, GPL-3.0): the page's AudioWorklet calls sim_render(n) and the device's clock runs
 * with the audio.
 *
 * The app's main loop never returns (app_main: for (;;) { platform_poll(); ... }, the splash polls too), so it runs
 * as a coroutine: Binaryen's Asyncify (wasm-opt --asyncify) lets platform_poll() park the whole loop and the next
 * millisecond resume it right there. One pass of the loop per millisecond of audio, as tests/host.c does.
 * The ISR is app_audio(256 frames) as on the device; sim_render serves the worklet's 128-frame quanta out of it. */
#define OBV_HOST 1
#define OBV_WASM 1
#include <stdint.h>
#include <stddef.h>

#define EXPORT __attribute__((used, visibility("default")))

/* the few libc calls (struct copies, zeroing; host.c's settings): bulk-memory instructions */
void *memset(void *d, int c, size_t n) { return __builtin_memset(d, c, n); }
void *memcpy(void *d, const void *s, size_t n) { return __builtin_memcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n) { return __builtin_memmove(d, s, n); }
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}
#define printf(...) ((void)0)                   /* (host.c's log lines: nowhere to go) */
static char *getenv(const char *n) { (void)n; return 0; }   /* (host.c's test switches: none in the browser) */

#include "../../firmware/src/obervave.c"

/* ---- Asyncify (wasm-opt --asyncify): the main loop as a coroutine */
__attribute__((import_module("asyncify"), import_name("start_unwind"))) void asyncify_start_unwind(void *p);
__attribute__((import_module("asyncify"), import_name("stop_unwind"))) void asyncify_stop_unwind(void);
__attribute__((import_module("asyncify"), import_name("start_rewind"))) void asyncify_start_rewind(void *p);
__attribute__((import_module("asyncify"), import_name("stop_rewind"))) void asyncify_stop_rewind(void);
static struct { void *pos, *end; } aco;         /* the parked loop's locals go here (Asyncify's data) */
static uint8_t aco_stack[16384] __attribute__((aligned(8)));
static volatile uint32_t parked, booted;

/* the app calls this every pass: park until the next millisecond. Not inlined: Asyncify instruments the callers of
 * the function that unwinds, and app_main must be one of them */
__attribute__((noinline)) static void platform_poll(void)
{
    if (!parked) {
        parked = 1;
        asyncify_start_unwind(&aco);
    } else {
        parked = 0;
        asyncify_stop_rewind();
    }
}
static void sim_app(void) { app_main(); }
static void (*volatile sim_app_p)(void) = sim_app;   /* (called through a pointer: app_main never returns, so a direct
                                                       call would let the compiler drop what follows it) */
__attribute__((noinline)) static void sim_pass(void)   /* one pass of the main loop (the first: boot, up to its first poll) */
{
    if (!booted) {
        booted = 1;
        aco.pos = aco_stack;
        aco.end = aco_stack + sizeof aco_stack;
    } else
        asyncify_start_rewind(&aco);
    sim_app_p();
    asyncify_stop_unwind();
}

/* ---- the clock: audio in the ISR's 256-frame blocks, the main loop every millisecond */
#define SIM_MAXN 1024u
static float out_l[SIM_MAXN], out_r[SIM_MAXN];
static int32_t blk[2u * HALF_FRAMES];
static uint32_t blk_pos, ms_acc;                /* (the first block is silence: the ISR's first half goes out before
                                                   anything was rendered; so the passes and the blocks take turns as in
                                                   tests/host.c, and the sound is its WAV's, 256 frames later) */
static int32_t master_q12 = 4096;               /* the MASTER knob (platform.c: (k10 * k10) >> 8), unity at first */
static int32_t peak;

EXPORT int sim_init(uint32_t rate)              /* 44100 only (the engine's tables) */
{
    if (rate != FS || booted) return booted ? 0 : -1;
    sim_pass();
    return 0;
}
EXPORT uint32_t sim_rate(void) { return FS; }
EXPORT float *sim_out_l(void) { return out_l; }
EXPORT float *sim_out_r(void) { return out_r; }
EXPORT void sim_render(uint32_t n)              /* n frames (<= 1024) into sim_out_l / sim_out_r, -1..1 */
{
    uint32_t i, j;
    if (!booted) return;
    for (i = 0; i < n && i < SIM_MAXN; i++) {
        if (blk_pos >= HALF_FRAMES) {           /* the ISR: a block, the MASTER knob, the device's clip */
            memset(blk, 0, sizeof blk);
            app_audio(blk, HALF_FRAMES);
            for (j = 0; j < 2u * HALF_FRAMES; j++) {
                int32_t v = (blk[j] * master_q12) >> 12;
                blk[j] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
                if (blk[j] > peak) peak = blk[j];
                else if (-blk[j] > peak) peak = -blk[j];
            }
            blk_pos = 0;
        }
        out_l[i] = (float)blk[2u * blk_pos] * (1.0f / 32768.0f);
        out_r[i] = (float)blk[2u * blk_pos + 1u] * (1.0f / 32768.0f);
        blk_pos++;
        if ((ms_acc += 1000u) >= FS) {           /* a millisecond: the clock, then a pass of the main loop */
            ms_acc -= FS;
            fm1_ms++;
            sim_pass();
        }
    }
}
EXPORT uint32_t sim_ms(void) { return fm1_ms; }
EXPORT int32_t sim_peak(void) { int32_t p = peak; peak = 0; return p; }   /* the loudest sample since the last call (Q15) */
EXPORT void sim_master(uint32_t k10)            /* the MASTER knob, 0..1023 (the device's curve); 1024: unity, as the
                                                   host build's WAV (tests/sim_test.mjs compares the two) */
{
    if (k10 > 1024u) k10 = 1024u;
    master_q12 = (int32_t)((k10 * k10) >> 8);
}

/* ---- the panel */
EXPORT void sim_key(uint32_t k, uint32_t down)  /* the 27 keys: 0 = F3 .. 26 = G5 */
{
    if (k >= 27u) return;
    if (down) fm1_in.notes |= 1u << k;
    else fm1_in.notes &= ~(1u << k);
}
EXPORT void sim_button(uint32_t b, uint32_t down)   /* BTN_* (platform.c): 0 OCT- 1 OCT+ 2 FX 3 SEL 4 ENV 5 LFO 6 EDIT
                                                       7 GLO 8 HOME 9 SAVE 10 ARP 11 SEQ 12 PLAY 13 REC */
{
    if (b >= 14u) return;
    if (down) {
        fm1_in.buttons |= 1u << b;
        host_edges |= 1u << b;
    } else {
        fm1_in.buttons &= ~(1u << b);
        if (host_edges >> b & 1u) host_rel_next |= 1u << b;   /* pressed and let go before the app saw it: a tap */
        else host_rel |= 1u << b;
    }
}
EXPORT void sim_enc(uint32_t e, int32_t steps)  /* ENC_*: 0 SELECT 1 ALGORITHM 2-5 KNOB 1-4 6 PRESETS; + = clockwise */
{
    if (e < (uint32_t)HOST_NENC) { host_enc[e] += steps; host_enc_pos[e] += steps; }
}
EXPORT void sim_midi(uint32_t st, uint32_t d1, uint32_t d2)   /* a MIDI message into the app's USB-MIDI in */
{
    if (st < 0x80u || st == 0xF0u || st == 0xF7u || st == 0xFEu || mq_w - mq_r >= 64u) return;
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)(d1 & 0x7Fu); mq[mq_w % 64u][2] = (uint8_t)(d2 & 0x7Fu);
    mq_w++;
}
EXPORT void sim_note(uint32_t note, uint32_t vel)   /* a note on MIDI channel 1 (vel 0 = off) */
{
    sim_midi(vel ? 0x90u : 0x80u, note, vel);
}

/* ---- what the panel shows */
EXPORT uint16_t *sim_fb(void) { return fb; }    /* 240 x 240 RGB565, row by row */
EXPORT uint32_t sim_fb_hash(void)               /* changes when the screen does */
{
    uint32_t h = 2166136261u, i;
    const uint32_t *p = (const uint32_t *)fb;
    for (i = 0; i < 240u * 240u / 2u; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
EXPORT uint32_t sim_leds_buttons(void) { return (uint32_t)(host_leds & 0x3FFFu); }       /* bit BTN_* lit */
EXPORT uint32_t sim_leds_keys(void) { return (uint32_t)(host_leds >> 14) & 0x7FFFFFFu; }  /* bit k: key k lit */
EXPORT uint32_t sim_glow_keys(void) { return (uint32_t)(host_glow >> 14) & 0x7FFFFFFu; }  /* the dim layer */
EXPORT uint32_t sim_page(void) { return page; }
EXPORT uint32_t sim_prog(void) { return trk_prog[0]; }   /* (track A's program) */

/* ---- storage: the settings sector (globals) and the 8 pattern slots (3.0: four tracks with their sounds), in RAM. The
 * page may keep them (localStorage) and put them back before sim_init: blob 0 = settings, 1..8 = patterns 1..8, byte
 * 0 = 1 if used, all HOST_BLOB_SZ long */
EXPORT uint32_t sim_blob_size(void) { return HOST_BLOB_SZ; }
EXPORT uint8_t *sim_blob(uint32_t i) { return i == 0u ? host_store : i <= NBLOB ? host_slot[i - 1u] : 0; }
EXPORT uint32_t sim_store_hash(void)            /* changes when a save happens */
{
    uint32_t h = 2166136261u, i, b;
    for (b = 0; b <= NBLOB; b++) {
        const uint8_t *p = sim_blob(b);
        for (i = 0; i < HOST_BLOB_SZ; i++) h = (h ^ p[i]) * 16777619u;
    }
    return h;
}
