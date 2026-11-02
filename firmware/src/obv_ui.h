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
#define C_TRK_B RGB(255, 150, 40)               /* track B's orange */
#define C_GREEN RGB(110, 231, 140)
/* the tracks' colours, everywhere: A the light blue accent, B orange, C green, D pink */
static const uint16_t C_TRKC[4] = {RGB(96, 176, 255), RGB(255, 150, 40), RGB(110, 220, 150), RGB(255, 96, 150)};
/* trk: the track the panel is on (its letter before the name, in its colour; the number in red for A) */
static void ui_header(uint32_t prog, const uint8_t *name, uint32_t edited, const char *tags, uint32_t trk)
{
    char n[PATCH_NAME + 2], tl[2] = {(char)('A' + trk % 4u), 0};
    uint32_t i;
    uint16_t c = C_TRKC[trk % 4u];
    cv_begin(240, 44, C_PANEL);
    cv_seg7(6, 6, (prog + 1u) / 10u, trk ? c : C_RED, C_RED_DIM);
    cv_seg7(26, 6, (prog + 1u) % 10u, trk ? c : C_RED, C_RED_DIM);
    for (i = 0; i < PATCH_NAME; i++) n[i] = (char)name[i];
    n[PATCH_NAME] = edited ? '*' : 0;
    n[PATCH_NAME + 1] = 0;
    cv_text(54, 4, &FONT_S, tl, c);
    cv_text(66, 4, &FONT_S, n, trk ? c : C_CREAM);
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

/* the sequencer's strip (instead of the scope): a line of text, then the 16 steps of track t from w0 (the page the
 * white keys play). A step: its chord as bars by pitch, dim when it plays by chance, grey when switched off, ticks
 * for a ratchet, a bridge to the next for a tie. The playhead's step is blue, the selected one framed in red. */
static void ui_steps(uint32_t t, uint32_t w0, int32_t cursor, int32_t playhead, uint32_t rec, const char *left, const char *right)
{
    uint32_t len = pat.trk[t].len, i, k;
    int32_t lo = 127, hi = 0;
    cv_begin(240, 50, C_SCOPE_BG);
    cv_text(4, 0, &FONT_S, left, C_CREAM);
    if (right) cv_text(236 - text_w(&FONT_S, right), 0, &FONT_S, right, rec ? C_RED : C_AMBER);
    if (!len) {
        cv_text(4, 24, &FONT_S, "TRACK OFF: GIVE IT A LENGTH", C_DIMT);
        cv_blit(0, 146);
        return;
    }
    for (i = 0; i < 16u && w0 + i < len; i++) {   /* the pitch range shown: the window's notes, at least an octave */
        const step_t *st = seq_step(t, w0 + i);
        for (k = 0; k < SEQ_CHORD && st->note[k]; k++) {
            if (st->note[k] < lo) lo = st->note[k];
            if (st->note[k] > hi) hi = st->note[k];
        }
    }
    if (hi < lo) { lo = 48; hi = 72; }
    if (hi - lo < 12) { lo -= (12 - (hi - lo)) / 2; hi = lo + 12; }
    for (i = 0; i < 16u; i++) {
        uint32_t n = w0 + i;
        int32_t x = (int32_t)i * 15, y = 19, h = 29;
        const step_t *st;
        uint16_t bg;
        if (n >= len) break;
        st = seq_step(t, n);
        bg = playhead >= 0 && n == (uint32_t)playhead ? STRIPE[0] : (n & 3u) ? C_KNOB : C_RULE;
        cv_rect(x + 1, y, 13, h, bg);
        if (n % 4u == 0u) cv_rect(x + 1, y, 13, 1, C_KNOB_HI);   /* the beats */
        if (!st->note[0]) cv_rect(x + 6, y + h / 2, 3, 2, C_DIMT);
        else {
            uint16_t c = st->ratch & STEP_OFF ? C_KNOB_HI : st->chance < 100u ? C_DIMT : C_CREAM;
            for (k = 0; k < SEQ_CHORD && st->note[k]; k++) {
                int32_t yy = y + h - 5 - (int32_t)(st->note[k] - lo) * (h - 9) / (hi - lo);
                cv_rect(x + 3, yy, 9, 2, c);
            }
            for (k = 0; k < (st->ratch & 3u); k++) cv_rect(x + 3 + (int32_t)k * 3, y + h - 3, 2, 2, C_AMBER);
            if (st->gate >= GATE_TIE && i < 15u) cv_rect(x + 12, y + h / 2 - 1, 5, 3, C_AMBER);
        }
        if (cursor >= 0 && n == (uint32_t)cursor) {   /* the selected step: a red frame */
            cv_rect(x, y - 1, 15, 2, C_RED); cv_rect(x, y + h - 1, 15, 2, C_RED);
            cv_rect(x, y - 1, 2, h + 1, C_RED); cv_rect(x + 13, y - 1, 2, h + 1, C_RED);
        }
    }
    cv_blit(0, 146);
}

/* the step view's hints (instead of the voice LEDs): what the keys do now, two lines */
static void ui_hints(const char *l1, const char *l2, uint16_t c1)
{
    cv_begin(240, 36, C_PANEL);
    cv_text((240 - text_w(&FONT_S, l1)) / 2, 1, &FONT_S, l1, c1);
    cv_text((240 - text_w(&FONT_S, l2)) / 2, 19, &FONT_S, l2, C_DIMT);
    cv_blit(0, 200);
}

/* ---- the full-screen sequencer (SEQ tapped), as SLOOP's STEP page but on the whole screen:
 *   0..23     the top line: the track, the view, the pattern, the tempo, PLAY / STOP / REC
 *   24..135   the 16 steps around the cursor as a piano roll (a bar per note: its width the gate, its brightness
 *             the velocity), the cursor framed in red, the playhead blue, step numbers under every beat
 *   136..187  a line (what the step holds, or a message), then up to 64 steps as small cells (the roll's 16 lit)
 *   188..239  the four knobs as label, value and a bar */
/* the tracks' colours, everywhere in the sequencer (the user: "not always clear which track you are in"):
 * C_TRKC above; ui_trk is the track drawn */
static uint32_t ui_trk;
#define C_TRK (C_TRKC[ui_trk % 4u])

static void ui_seq_top(uint32_t t, const char *view, const char *mid, const char *right, uint16_t rc, uint32_t on)
{
    uint32_t i;
    cv_begin(240, 24, C_PANEL);
    for (i = 0; i < 4u; i++) {                  /* the four tracks: the one shown filled, the others a frame (dim: stopped) */
        char tl[2] = {(char)('A' + i), 0};
        int32_t x = 3 + (int32_t)i * 15;
        uint16_t c = C_TRKC[i];
        if (i == t) { cv_rect(x, 3, 13, 17, c); cv_text(x + 7 - text_w(&FONT_S, tl) / 2, 3, &FONT_S, tl, C_PANEL); }
        else {
            uint16_t d = on >> i & 1u ? blend565(C_PANEL, c, 170) : C_KNOB_HI;
            cv_rect(x, 3, 13, 1, d); cv_rect(x, 19, 13, 1, d); cv_rect(x, 3, 1, 17, d); cv_rect(x + 12, 3, 1, 17, d);
            cv_text(x + 7 - text_w(&FONT_S, tl) / 2, 3, &FONT_S, tl, d);
        }
    }
    cv_text(66, 3, &FONT_S, view, C_TRK);
    cv_text(70 + text_w(&FONT_S, view), 3, &FONT_S, mid, C_CREAM);
    cv_text(236 - text_w(&FONT_S, right), 3, &FONT_S, right, rc);
    cv_rect(0, 23, 240, 1, C_RULE);
    cv_blit(0, 0);
}

static uint16_t seq_note_c(const step_t *st)    /* a note's colour: its velocity, dim by chance, grey when off */
{
    if (st->ratch & STEP_OFF) return C_KNOB_HI;
    return blend565(st->chance < 100u ? C_RULE : blend565(C_PANEL, C_TRK, 110), st->chance < 100u ? blend565(C_PANEL, C_TRK, 150) : C_TRK, (uint32_t)st->vel * 2u);
}

static void ui_seq_roll(uint32_t t, uint32_t w0, int32_t cursor, int32_t playhead)
{
    static const int32_t TOP = 4, AH = 88;      /* the notes' area */
    uint32_t len = pat.trk[t].len, i, k;
    int32_t lo = 127, hi = 0;
    cv_begin(240, 112, C_SCOPE_BG);
    cv_rect(0, 0, 240, 1, C_TRK);               /* (the track's colour round the roll) */
    for (i = 0; i < 16u && w0 + i < len; i++) {   /* the pitch range: the window's notes, at least an octave */
        const step_t *st = seq_step(t, w0 + i);
        for (k = 0; k < SEQ_CHORD && st->note[k]; k++) {
            if (st->note[k] < lo) lo = st->note[k];
            if (st->note[k] > hi) hi = st->note[k];
        }
    }
    if (hi < lo) { lo = 54; hi = 66; }
    if (hi - lo < 12) { lo -= (12 - (hi - lo)) / 2; hi = lo + 12; }
    for (i = 0; i < 16u; i++) {
        uint32_t n = w0 + i;
        int32_t x = (int32_t)i * 15;
        const step_t *st;
        if (n >= len) { cv_rect(x + 1, TOP, 13, AH, C_PANEL); continue; }
        st = seq_step(t, n);
        cv_rect(x + 1, TOP, 13, AH, playhead >= 0 && n == (uint32_t)playhead ? STRIPE[0]
                                    : cursor >= 0 && n == (uint32_t)cursor ? C_RULE : n % 4u ? C_KNOB : RGB(44, 52, 68));
        if (n % 4u == 0u) {                     /* the beats: a tick and the step's number */
            char s[4];
            uint32_t v = n + 1u, j = 0;
            if (v >= 100u) s[j++] = (char)('0' + v / 100u);
            if (v >= 10u) s[j++] = (char)('0' + v / 10u % 10u);
            s[j++] = (char)('0' + v % 10u);
            s[j] = 0;
            cv_rect(x + 1, TOP, 13, 1, C_KNOB_HI);
            cv_text(x + 2, TOP + AH + 3, &FONT_S, s, cursor >= 0 && n == (uint32_t)cursor ? C_CREAM : C_DIMT);
        }
        if (!st->note[0]) cv_rect(x + 5, TOP + AH / 2, 5, 2, C_KNOB_HI);
        else {
            uint16_t c = seq_note_c(st);
            int32_t w = st->gate >= GATE_TIE ? 15 : clampi(st->gate * 11 / 120, 3, 11);
            for (k = 0; k < SEQ_CHORD && st->note[k]; k++) {
                int32_t y = TOP + 2 + (hi - st->note[k]) * (AH - 8) / (hi - lo);
                cv_rect(x + 2, y, i == 15u && w > 11 ? 12 : w, 4, c);   /* a tie runs on into the next step */
            }
            for (k = 0; k < (st->ratch & 3u); k++) cv_rect(x + 3 + (int32_t)k * 3, TOP + AH - 4, 2, 2, C_AMBER);
        }
        if (cursor >= 0 && n == (uint32_t)cursor) {   /* the cursor: a red frame */
            cv_rect(x, TOP - 2, 15, 2, C_RED); cv_rect(x, TOP + AH, 15, 2, C_RED);
            cv_rect(x, TOP - 2, 2, AH + 4, C_RED); cv_rect(x + 13, TOP - 2, 2, AH + 4, C_RED);
        }
    }
    cv_blit(0, 24);
}

static void ui_seq_info(uint32_t t, const char *line, uint16_t lc, const char *right, uint32_t w0, int32_t cursor, int32_t playhead)
{
    uint32_t len = pat.trk[t].len, win = w0 / 64u * 64u, i;
    cv_begin(240, 52, C_PANEL);
    cv_text(4, 0, &FONT_S, line, lc);
    if (right) cv_text(236 - text_w(&FONT_S, right), 0, &FONT_S, right, right[0] == 'P' ? C_CREAM : C_DIMT);   /* (PLAYS n: bright) */
    for (i = 0; i < 64u; i++) {                 /* the overview: four rows of 16 */
        uint32_t n = win + i;
        int32_t x = (int32_t)(i % 16u) * 15 + 1, y = 20 + (int32_t)(i / 16u) * 8;
        const step_t *st;
        uint16_t c;
        if (n >= len) break;
        st = seq_step(t, n);
        if (cursor >= 0 && n == (uint32_t)cursor) c = C_RED;
        else if (playhead >= 0 && n == (uint32_t)playhead) c = STRIPE[2];
        else if (st->note[0]) c = seq_note_c(st);
        else c = n / 16u == w0 / 16u ? C_KNOB_HI : C_KNOB;
        cv_rect(x, y, 13, 6, c);
    }
    cv_blit(0, 136);
}

static void ui_seq_knobs(const knob_view_t *k)
{
    int32_t i;
    cv_begin(240, 52, C_PANEL);
    cv_rect(0, 0, 240, 1, C_RULE);
    for (i = 0; i < 4; i++) {
        int32_t cx = 30 + 60 * i, f;
        if (!k[i].present) continue;
        cv_text(cx - text_w(&FONT_S, k[i].label) / 2, 3, &FONT_S, k[i].label, C_DIMT);
        cv_text(cx - text_w(&FONT_S, k[i].value) / 2, 19, &FONT_S, k[i].value, C_CREAM);
        cv_rect(cx - 22, 42, 44, 3, C_KNOB);
        if (k[i].max > 0) {
            f = 44 * clampi(k[i].v, 0, k[i].max) / k[i].max;
            if (k[i].bip) { if (f >= 22) cv_rect(cx, 42, f - 22, 3, C_TRK); else cv_rect(cx - 22 + f, 42, 22 - f, 3, C_TRK); }
            else cv_rect(cx - 22, 42, f, 3, C_TRK);
        }
    }
    cv_blit(0, 188);
}

/* TRACKS (as SLOOP's): A-D one under the other, 36 px each from y 24, the chosen one lit in its colour. A row: the
 * letter in a tile, the sound (number and name), its state on the right (PLAY, STOP, NEXT: starts on the bar, ENDS:
 * stops on the bar, MUTE), the 16 steps of the page that plays (stopped: the cursor's), the playhead under its step */
static void ui_seq_track_row(uint32_t t, uint32_t sel, const char *title, uint16_t tc, const char *status, uint16_t sc,
                             uint32_t w0, int32_t playhead)
{
    uint32_t len = pat.trk[t].len, i;
    uint16_t c = C_TRKC[t % 4u], dim = blend565(C_PANEL, c, 110);
    char tl[2] = {(char)('A' + t), 0};
    cv_begin(240, 36, sel ? C_KNOB : C_PANEL);
    if (sel) cv_rect(0, 0, 3, 36, c);
    if (sel) { cv_rect(7, 7, 22, 22, c); cv_text(18 - text_w(&FONT_S, tl) / 2, 10, &FONT_S, tl, C_PANEL); }
    else {
        cv_rect(7, 7, 22, 1, dim); cv_rect(7, 28, 22, 1, dim); cv_rect(7, 7, 1, 22, dim); cv_rect(28, 7, 1, 22, dim);
        cv_text(18 - text_w(&FONT_S, tl) / 2, 10, &FONT_S, tl, dim);
    }
    cv_text(36, 2, &FONT_S, title, sel ? tc : blend565(C_PANEL, tc, 170));
    if (status[0]) cv_text(236 - text_w(&FONT_S, status), 2, &FONT_S, status, sc);
    for (i = 0; i < 16u; i++) {
        uint32_t n = w0 + i;
        int32_t x = 36 + (int32_t)i * 12 + (i > 7u ? 2 : 0);
        uint16_t cc;
        if (n >= len) { cv_rect(x, 21, 10, 9, sel ? C_PANEL : C_KNOB); continue; }
        if (seq_step(t, n)->note[0] && !(seq_step(t, n)->ratch & STEP_OFF)) cc = sel ? c : dim;
        else cc = n % 4u ? (sel ? C_PANEL : C_KNOB) : C_KNOB_HI;
        cv_rect(x, 21, 10, 9, cc);
        if (playhead >= 0 && n == (uint32_t)playhead) cv_rect(x, 31, 10, 2, C_CREAM);
    }
    if (len > 16u) {                            /* the whole track under it: a piece per bar, filled up to where it plays */
        uint32_t bars = (len + 15u) / 16u, k, pg = playhead >= 0 ? (uint32_t)playhead / 16u : 0xFFFFu;
        for (k = 0; k < bars; k++) {
            int32_t x0 = 36 + (int32_t)(k * 194u / bars), x1 = 36 + (int32_t)((k + 1u) * 194u / bars) - 2, w = x1 - x0;
            uint32_t in = len - k * 16u < 16u ? len - k * 16u : 16u;
            cv_rect(x0, 33, w, 1, sel ? C_PANEL : C_KNOB);
            if (k < pg) cv_rect(x0, 33, w, 1, dim);
            else if (k == pg) cv_rect(x0, 33, (int32_t)(((uint32_t)playhead - k * 16u + 1u) * (uint32_t)w / in), 1, C_CREAM);
        }
    }
    cv_rect(0, 35, 240, 1, C_RULE);
    cv_blit(0, 24 + (int32_t)t * 36);
}

/* the mixer page's strip (instead of the scope and the voice lights): per track a column with its letter, its level
 * as a bar, its state (PLAY, STOP, NEXT, ENDS), MUTE and SOLO lit; the chosen track framed */
static void ui_mix_strip(const uint8_t *lvl, uint32_t mute, uint32_t solo, const uint32_t *state, uint32_t sel, uint32_t blink,
                         const uint8_t *meter)   /* (VIBE) meter: what each track plays now, 0..44 px */
{
    static const char *const ST[5] = {"STOP", "PLAY", "NEXT", "ENDS", "OFF"};
    uint32_t t;
    cv_begin(240, 94, C_PANEL);
    for (t = 0; t < 4u; t++) {
        int32_t x = (int32_t)t * 60 + 3, h = (int32_t)lvl[t] * 44 / 127;
        uint16_t c = C_TRKC[t], dim = blend565(C_PANEL, c, 90);
        char tl[2] = {(char)('A' + t), 0};
        const char *st = ST[state[t] > 4u ? 4u : state[t]];   /* (4: no length, off) */
        uint16_t stc = state[t] == 1u ? C_GREEN : state[t] == 2u || state[t] == 3u ? (blink ? C_AMBER : C_DIMT) : C_DIMT;
        if (t == sel) { cv_rect(x - 2, 0, 58, 1, c); cv_rect(x - 2, 93, 58, 1, c); cv_rect(x - 2, 0, 1, 94, c); cv_rect(x + 55, 0, 1, 94, c); }
        cv_rect(x + 2, 4, 18, 18, c);
        cv_text(x + 11 - text_w(&FONT_S, tl) / 2, 5, &FONT_S, tl, C_PANEL);
        cv_rect(x + 26, 4, 8, 44, C_KNOB);                    /* the level */
        cv_rect(x + 26, 48 - h, 8, h, mute >> t & 1u || (solo && !(solo >> t & 1u)) ? C_KNOB_HI : c);
        cv_rect(x + 2, 28, 18, 20, dim);
        {   /* (VIBE) the meter beside the level: green, from -6 dB amber, near full red */
            int32_t mh = meter[t] > 44u ? 44 : (int32_t)meter[t], j;
            cv_rect(x + 37, 4, 6, 44, C_KNOB);
            for (j = 0; j < mh; j += 2) cv_rect(x + 37, 46 - j, 6, 1, j >= 40 ? C_RED : j >= 31 ? RGB(255, 200, 40) : C_GREEN);
        }
        cv_text(x + 2, 52, &FONT_S, st, stc);
        cv_rect(x + 2, 72, 24, 18, mute >> t & 1u ? C_RED : C_KNOB);
        cv_text(x + 14 - text_w(&FONT_S, "M") / 2, 73, &FONT_S, "M", mute >> t & 1u ? C_PANEL : C_DIMT);
        cv_rect(x + 30, 72, 24, 18, solo >> t & 1u ? RGB(255, 210, 60) : C_KNOB);
        cv_text(x + 42 - text_w(&FONT_S, "S") / 2, 73, &FONT_S, "S", solo >> t & 1u ? C_PANEL : C_DIMT);
    }
    cv_blit(0, 146);
}

static void ui_seq_tline(const char *l, uint16_t c)   /* TRACKS: the line under the four rows */
{
    cv_begin(240, 20, C_PANEL);
    cv_text((240 - text_w(&FONT_S, l)) / 2, 2, &FONT_S, l, c);
    cv_blit(0, 168);
}

/* PATTERNS: the eight slots as two columns of four (selected: framed in red; the one playing: a blue bar) */
static void ui_seq_slots(const char *const *txt, uint32_t used, uint32_t sel, uint32_t now, uint32_t now_edited)
{
    uint32_t i;
    cv_begin(240, 112, C_PANEL);
    for (i = 0; i < 8u; i++) {
        int32_t x = (int32_t)(i % 2u) * 120 + 2, y = (int32_t)(i / 2u) * 28 + 2;
        char num[3] = {(char)('1' + i), i == now && now_edited ? '*' : 0, 0};
        cv_rect(x, y, 116, 24, i == sel ? C_RULE : C_KNOB);
        if (i == now) cv_rect(x, y, 4, 24, STRIPE[1]);
        cv_text(x + 10, y + 4, &FONT_S, num, i == now ? C_AMBER : C_CREAM);
        cv_text(x + 34, y + 4, &FONT_S, txt[i], used >> i & 1u ? C_CREAM : C_DIMT);
        if (i == sel) {
            cv_rect(x - 2, y - 2, 120, 2, C_RED); cv_rect(x - 2, y + 24, 120, 2, C_RED);
            cv_rect(x - 2, y - 2, 2, 28, C_RED); cv_rect(x + 116, y - 2, 2, 28, C_RED);
        }
    }
    cv_blit(0, 24);
}

static void ui_seq_lines(const char *l1, uint16_t c1, const char *l2, const char *l3)
{
    cv_begin(240, 52, C_PANEL);
    cv_text(4, 0, &FONT_S, l1, c1);
    cv_text(4, 18, &FONT_S, l2, C_CREAM);
    cv_text(4, 34, &FONT_S, l3, C_DIMT);
    cv_blit(0, 136);
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
        const char *t1 = "4-TRACK ANALOG POLY", *t2 = "M-VAVE FM-1";
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
