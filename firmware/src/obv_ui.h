/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen: an own design (no copied trade dress). A blue-black panel with light blue legends, as the classic
 * American polys had them, a red LED program number drawn as 7-segment digits, "OBERVAVE" with three blue bands
 * through the lower half of the letters, four anti-aliased round knobs with a blue value arc, a scope strip, eight
 * round voice LEDs. 240 x 240:
 *   0..43    header: program number, name, tags, the logo
 *   46..143  the page: title between rules, four knobs (label above, value below)
 *   146..195 the scope
 *   200..236 the voice LEDs */
#define C_PANEL RGB(14, 18, 28)                 /* blue-black */
#define C_CREAM RGB(226, 232, 240)              /* the legends: near white, a little cold */
#define C_AMBER RGB(96, 176, 255)               /* the accent: the light blue of the originals' silkscreen */
#define C_RED RGB(255, 44, 32)
#define C_RED_DIM RGB(62, 18, 14)
#define C_DIMT RGB(120, 142, 172)
#define C_RULE RGB(48, 60, 84)
#define C_KNOB RGB(38, 44, 56)
#define C_KNOB_HI RGB(84, 96, 118)
#define C_SCOPE_BG RGB(8, 12, 22)
#define C_SCOPE_MID RGB(28, 40, 64)
static const uint16_t STRIPE[3] = {RGB(30, 70, 190), RGB(60, 130, 235), RGB(120, 190, 255)};

/* a * (1 - t) + b * t for RGB565, t = 0..256 */
static uint16_t blend565(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t r = ((a >> 11) * (256u - t) + (b >> 11) * t) >> 8;
    uint32_t g = (((a >> 5) & 63u) * (256u - t) + ((b >> 5) & 63u) * t) >> 8;
    uint32_t bl = ((a & 31u) * (256u - t) + (b & 31u) * t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static inline uint16_t cv_get(int32_t x, int32_t y)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
    return C_PANEL;
}

/* an anti-aliased disc: coverage from the distance to the centre (pixel centres at +0.5, so in doubled units),
 * blended over what is there. r in pixels, ring: 0 = full disc, else only r - ring .. r */
static void cv_disc(int32_t cx, int32_t cy, int32_t r, int32_t ring, uint16_t c)
{
    int32_t x, y, ro2 = (2 * r + 1) * (2 * r + 1), ri2 = (2 * r - 1) * (2 * r - 1);
    int32_t in2 = ring ? (2 * (r - ring) + 1) * (2 * (r - ring) + 1) : -1, in1 = ring ? (2 * (r - ring) - 1) * (2 * (r - ring) - 1) : -1;
    for (y = cy - r - 1; y <= cy + r + 1; y++)
        for (x = cx - r - 1; x <= cx + r + 1; x++) {
            int32_t dx = 2 * (x - cx), dy = 2 * (y - cy), d2 = dx * dx + dy * dy, t;
            if (d2 >= ro2 || (ring && d2 < in1))
                continue;
            t = 256;
            if (d2 > ri2)                       /* the outer edge: 1 px of coverage */
                t = 256 * (ro2 - d2) / (ro2 - ri2);
            if (ring && d2 < in2)               /* the inner edge */
                t = 256 * (d2 - in1) / (in2 - in1);
            cv_pset(x, y, t >= 256 ? c : blend565(cv_get(x, y), c, (uint32_t)t));
        }
}

/* a line of width 2, for the arcs and pointers */
static void cv_line2(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0 + 1, y0, x1 + 1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}

/* the knob's angle: value 0 at 225 deg (lower left), max at -45 deg (lower right), 270 deg of travel */
static void knob_xy(int32_t cx, int32_t cy, int32_t rad, int32_t deg, int32_t *x, int32_t *y)
{
    int32_t i;
    while (deg < 0) deg += 360;
    while (deg >= 360) deg -= 360;
    i = deg / 5;
    *x = cx + ((rad * COS_Q8[i]) >> 8);
    *y = cy - ((rad * COS_Q8[(i + 54u) % 72u]) >> 8);   /* sin = cos(deg - 90): 90 deg = 18 steps back = 54 on */
}

static void cv_arc(int32_t cx, int32_t cy, int32_t rad, int32_t deg0, int32_t deg1, uint16_t c)   /* clockwise on screen */
{
    int32_t d, x0, y0, x1, y1;
    knob_xy(cx, cy, rad, deg0, &x0, &y0);
    for (d = deg0 - 5; d >= deg1; d -= 5) {
        knob_xy(cx, cy, rad, d, &x1, &y1);
        cv_line2(x0, y0, x1, y1, c);
        x0 = x1; y0 = y1;
    }
}

/* a knob: label above, the value's arc and pointer, the value below. v / max, bip: the arc grows from the middle */
static void cv_knob(int32_t cx, int32_t cy, const char *label, const char *value, int32_t v, int32_t max, int32_t bip)
{
    int32_t ang = 225 - (max ? 270 * v / max : 0), px, py, qx, qy, r = 15;
    cv_text(cx - text_w(&FONT_S, label) / 2, cy - 44, &FONT_S, label, C_DIMT);
    cv_arc(cx, cy, r + 6, 225, -45, C_RULE);
    if (bip) {
        if (ang < 90) cv_arc(cx, cy, r + 6, 90, ang, C_AMBER);
        else cv_arc(cx, cy, r + 6, ang, 90, C_AMBER);
    } else
        cv_arc(cx, cy, r + 6, 225, ang, C_AMBER);
    cv_disc(cx, cy, r, 0, C_KNOB);
    cv_disc(cx, cy, r, 2, C_KNOB_HI);
    knob_xy(cx, cy, 5, ang, &px, &py);
    knob_xy(cx, cy, r - 2, ang, &qx, &qy);
    cv_line2(px, py, qx, qy, C_CREAM);
    cv_text(cx - text_w(&FONT_S, value) / 2, cy + 24, &FONT_S, value, C_CREAM);
}

/* 7-segment digit, w 16 x h 28, thickness 4: segments a (top) b c d (bottom) e f g (middle) */
static void cv_seg7(int32_t x, int32_t y, uint32_t digit, uint16_t on, uint16_t off)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    uint32_t m = SEG[digit % 10u];
    const int32_t t = 4, w = 16, h = 28;
    cv_rect(x + 2, y, w - 4, t, m & 1u ? on : off);                       /* a */
    cv_rect(x + w - t, y + 2, t, h / 2 - 3, m & 2u ? on : off);           /* b */
    cv_rect(x + w - t, y + h / 2 + 1, t, h / 2 - 3, m & 4u ? on : off);   /* c */
    cv_rect(x + 2, y + h - t, w - 4, t, m & 8u ? on : off);               /* d */
    cv_rect(x, y + h / 2 + 1, t, h / 2 - 3, m & 16u ? on : off);          /* e */
    cv_rect(x, y + 2, t, h / 2 - 3, m & 32u ? on : off);                  /* f */
    cv_rect(x + 2, y + h / 2 - 2, w - 4, t, m & 64u ? on : off);          /* g */
}

/* the stripes: three bands of `band` px from row y0. In the letters (text_only, the logo) the text takes the
 * band's colour by its brightness; across the screen (the splash) the band runs up to x_to and the letters
 * stand in it as dark cut-outs */
static void cv_stripes(int32_t y0, int32_t band, int32_t gap, int32_t x_to, int32_t text_only)
{
    int32_t b, y, x;
    for (b = 0; b < 3; b++)
        for (y = y0 + b * (band + gap); y < y0 + b * (band + gap) + band; y++)
            for (x = 0; x < (int32_t)cv_w; x++) {
                uint16_t c = cv_get(x, y);
                uint32_t g = (c >> 5) & 63u, is_text = c != C_PANEL && c != C_BLACK && g > 14u;   /* brighter than the panel */
                if (text_only) {
                    if (is_text) cv_pset(x, y, blend565(C_PANEL, STRIPE[b], g * 256u / 63u));
                } else if (x < x_to)
                    cv_pset(x, y, is_text ? blend565(STRIPE[b], C_PANEL, g * 256u / 63u) : STRIPE[b]);
            }
}

static void draw_logo_small(int32_t x, int32_t y)   /* "OBERVAVE" in FONT_S with the bands through the lower half */
{
    cv_text(x, y, &FONT_S, "OBERVAVE", C_CREAM);
    cv_stripes(y + 10, 2, 0, 0, 1);
}

/* ---- the parts of the screen (the app decides when) */
static void ui_header(uint32_t prog, const uint8_t *name, uint32_t edited, const char *tags)
{
    char n[PATCH_NAME + 2];
    uint32_t i;
    cv_begin(240, 44, C_PANEL);
    cv_seg7(6, 6, (prog + 1u) / 10u, C_RED, C_RED_DIM);
    cv_seg7(26, 6, (prog + 1u) % 10u, C_RED, C_RED_DIM);
    for (i = 0; i < PATCH_NAME; i++) n[i] = (char)name[i];
    n[PATCH_NAME] = edited ? '*' : 0;
    n[PATCH_NAME + 1] = 0;
    cv_text(54, 4, &FONT_S, n, C_CREAM);
    cv_text(54, 22, &FONT_S, tags, C_DIMT);
    draw_logo_small(240 - 4 - text_w(&FONT_S, "OBERVAVE"), 4);
    cv_rect(0, 42, 240, 1, C_RULE);
    cv_blit(0, 0);
}

typedef struct { const char *label; char value[12]; int32_t v, max, bip, present; } knob_view_t;
static void ui_page(const char *title, const knob_view_t *k, const char *note)
{
    int32_t tw = text_w(&FONT_S, title), i;
    cv_begin(240, 98, C_PANEL);
    cv_rect(6, 9, (240 - tw) / 2 - 14, 1, C_RULE);
    cv_text((240 - tw) / 2, 1, &FONT_S, title, C_AMBER);
    cv_rect((240 + tw) / 2 + 8, 9, (240 - tw) / 2 - 14, 1, C_RULE);
    for (i = 0; i < 4; i++)
        if (k[i].present) {
            cv_knob(30 + 60 * i, 62, k[i].label, k[i].value, k[i].v, k[i].max, k[i].bip);
            app_idle();                         /* (a knob is ~1 ms: the keys and MIDI in between) */
        }
    if (note)
        cv_text(240 - 4 - text_w(&FONT_S, note), 1, &FONT_S, note, C_RED);
    cv_blit(0, 46);
}

/* HOLD HOME: the control map of the page you are on. Four strips (the canvas is 120 rows at most on the host), each
 * followed by app_idle(): the DRAW RULE (CLAUDE.md section 6), a draw that takes time lets the notes through.
 *   top    the page's name
 *   knobs  KNOB 1-4 and what each one is on this page ("-" = none)
 *   rows   left: where each page button goes NOW (the live part); right: what the rest of the panel does
 *   foot   what letting go does */
typedef struct { const char *name, *what; } map_row_t;
#define MAP_ROWS 6
static void ui_map(const char *title, const knob_view_t *k, const char *knob_note, const map_row_t *l, const map_row_t *r,
                   int32_t hi_l, int32_t hi_r, const char *foot)
{
    int32_t i;
    cv_begin(240, 44, C_PANEL);
    cv_text((240 - text_w(&FONT_S, title)) / 2, 4, &FONT_S, title, C_AMBER);
    cv_text((240 - text_w(&FONT_S, "CONTROL MAP")) / 2, 22, &FONT_S, "CONTROL MAP", C_DIMT);
    cv_rect(0, 42, 240, 1, C_RULE);
    cv_blit(0, 0);
    app_idle();
    cv_begin(240, 50, C_PANEL);
    for (i = 0; i < 4; i++) {
        char n[8];
        n[0] = 'K'; n[1] = 'N'; n[2] = 'O'; n[3] = 'B'; n[4] = ' '; n[5] = (char)('1' + i); n[6] = 0;
        cv_text(30 + 60 * i - text_w(&FONT_S, n) / 2, 2, &FONT_S, n, C_DIMT);
        if (k[i].present)
            cv_text(30 + 60 * i - text_w(&FONT_S, k[i].label) / 2, 22, &FONT_S, k[i].label, C_CREAM);
        else if (!knob_note)
            cv_text(30 + 60 * i - text_w(&FONT_S, "-") / 2, 22, &FONT_S, "-", C_RULE);
        if (i && !knob_note) cv_rect(60 * i, 4, 1, 40, C_RULE);
    }
    if (knob_note)
        cv_text((240 - text_w(&FONT_S, knob_note)) / 2, 22, &FONT_S, knob_note, C_DIMT);
    cv_blit(0, 44);
    app_idle();
    cv_begin(240, 116, C_PANEL);
    cv_rect(0, 1, 240, 1, C_RULE);
    cv_rect(124, 8, 1, 6 * 18 - 4, C_RULE);
    for (i = 0; i < MAP_ROWS; i++) {
        int32_t y = 6 + i * 18;
        cv_text(5, y, &FONT_S, l[i].name, i == hi_l ? C_AMBER : C_CREAM);
        cv_text(43, y, &FONT_S, l[i].what, i == hi_l ? C_AMBER : C_DIMT);
        cv_text(130, y, &FONT_S, r[i].name, i == hi_r ? C_AMBER : C_CREAM);
        cv_text(236 - text_w(&FONT_S, r[i].what), y, &FONT_S, r[i].what, i == hi_r ? C_AMBER : C_DIMT);
        app_idle();                             /* (a row is ~1 ms: the keys and MIDI in between) */
    }
    cv_blit(0, 96);
    app_idle();
    cv_begin(240, 24, C_PANEL);
    cv_text((240 - text_w(&FONT_S, foot)) / 2, 2, &FONT_S, foot, C_DIMT);
    cv_blit(0, 216);
    app_idle();
}

static void ui_scope(const int16_t *s, uint32_t n)
{
    uint32_t i;
    cv_begin(240, 50, C_SCOPE_BG);
    cv_rect(0, 25, 240, 1, C_SCOPE_MID);
    for (i = 1; i < n; i++)
        cv_line((int32_t)i - 1, 25 - clampi(s[i - 1u] >> 9, -23, 23), (int32_t)i, 25 - clampi(s[i] >> 9, -23, 23), C_AMBER);
    cv_blit(0, 146);
}

static void ui_leds(const volatile int32_t *level, uint32_t n)
{
    uint32_t v;
    cv_begin(240, 36, C_PANEL);
    for (v = 0; v < n; v++) {
        uint32_t l = (uint32_t)level[v] >> 7;   /* 0..255 */
        uint16_t c = blend565(C_RED_DIM, RGB(255, 70, 40), l), halo = blend565(C_PANEL, RGB(255, 70, 40), l / 4u);
        int32_t cx = 22 + (int32_t)v * 28;
        if (l > 8u) cv_disc(cx, 17, 10, 0, halo);
        cv_disc(cx, 17, 6, 0, c);
        cv_disc(cx, 17, 6, 1, blend565(C_PANEL, c, 160));
    }
    cv_blit(0, 200);
}

/* the step view's hints (instead of the voice LEDs): what the keys do now, two lines */
static void ui_hints(const char *l1, const char *l2, uint16_t c1)
{
    cv_begin(240, 36, C_PANEL);
    cv_text((240 - text_w(&FONT_S, l1)) / 2, 1, &FONT_S, l1, c1);
    cv_text((240 - text_w(&FONT_S, l2)) / 2, 19, &FONT_S, l2, C_DIMT);
    cv_blit(0, 200);
}

/* the splash: the logo (firmware/gen/obv_splash.h, from tools/make_logo.py --splash). The glowing letters strike
 * like a neon tube (a few flickers, then full), the three bands sweep in from the left, the line under it fades in.
 * ~2.4 s, polls meanwhile. Both layers are RLE over one palette; they are read in step, pixel by pixel */
#include "obv_splash.h"
#define SPLASH_Y 70
static void splash_frame(uint32_t fade, int32_t x_to, uint32_t sub)
{
    const uint8_t *a = SPLASH_RLE0, *b = SPLASH_RLE1;     /* the letters; the letters with the bands */
    uint32_t ra = 0, rb = 0, ia = 0, ib = 0, x = 0, y = 0, n = SPLASH_W * SPLASH_H, i;
    cv_begin(240, SPLASH_H + 40, C_PANEL);
    for (i = 0; i < n; i++) {
        uint16_t c;
        if (!ra) { ra = *a++; ia = *a++; }
        if (!rb) { rb = *b++; ib = *b++; }
        ra--; rb--;
        c = SPLASH_PAL[(int32_t)x < x_to ? ib : ia];
        if (c != C_PANEL)
            cv_px[y * 240u + x] = swap16(fade >= 256u ? c : blend565(C_PANEL, c, fade));
        if (++x == SPLASH_W) { x = 0; y++; }
    }
    if (sub) {                                      /* the line under the logo, as in assets/logo.png, in two rows */
        const char *t1 = "8-VOICE ANALOG POLY", *t2 = "M-VAVE FM-1";
        uint16_t c = sub >= 256u ? C_AMBER : blend565(C_PANEL, C_AMBER, sub);
        cv_text((240 - text_w(&FONT_S, t1)) / 2, SPLASH_H + 14, &FONT_S, t1, c);
        cv_text((240 - text_w(&FONT_S, t2)) / 2, SPLASH_H + 28, &FONT_S, t2, c);
    }
    cv_blit(0, SPLASH_Y);
}

static void ui_splash(void)
{
    /* the strike: dark, two short flashes, a dip, then it holds (per 40 ms frame, 0..256) */
    static const uint16_t STRIKE[12] = {0, 90, 20, 0, 200, 60, 30, 256, 180, 256, 256, 256};
    uint32_t t0 = fm1_ms, k;
    lcd_fill(0, 0, 240, 240, C_PANEL);
    for (k = 0; k < 30u; k++) {
        uint32_t fade = k < 12u ? STRIKE[k] : 256u, sub = k < 16u ? 0u : (k - 16u) * 32u;
        int32_t x_to = k < 10u ? 0 : (int32_t)((k - 10u) * (k - 10u) * 240u / 196u) + 2;   /* eases in, 10 .. 24 */
        splash_frame(fade, x_to, sub);
        while (fm1_ms - t0 < 40u * (k + 1u)) platform_poll();
    }
    while (fm1_ms - t0 < 2400u) platform_poll();     /* it stands for a while: 2.4 s from power-on */
    lcd_fill(0, 0, 240, 240, C_PANEL);
}
