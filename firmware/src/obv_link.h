/* SPDX-License-Identifier: GPL-3.0-only */
/* VIBE LINK (3.4, 7.11.2026, Sven: "Link in OberVave rein"): the FM-1 as a controller for the VIBE FM, inside OberVave.
 * The same MIDI as the small VIBE FM Link firmware (sloopDX/vibe-fm-link), so the VIBE's one profile fits both:
 *   KNOB 1-4 CC 20-23, SELECT CC 24, ALGORITHM CC 25, PRESETS CC 26 – relative, two's complement (+n = n, -n = 128 - n)
 *   MASTER CC 7 (0..127)
 *   buttons CC 40 + id, 127 pressed / 0 let go (FX 42, SEL 43, ENV 44, LFO 45, EDIT 46, GLO 47, HOME 48, SAVE 49,
 *   ARP 50, SEQ 51, PLAY 52, REC 53); SAVE held = SHIFT: SAVE + PLAY = STOP (CC 54), SAVE + OCT- / OCT+ = LEFT /
 *   RIGHT (56 / 57); GLO held + EDIT ENV LFO ARP FX SEQ = F1..F6 (CC 60-65), GLO tapped alone = MENU (47)
 *   keys: notes, as OberVave sends them anyway (MIDI_OUT_CH)
 * set.link: 0 OFF · 1 ON – OberVave plays on and everything also goes out (a knob turns the sound here and the
 * VIBE's encoder there) · 2 CONTROL – only the controller: nothing sounds here, the panel is the VIBE's, the screen
 * says so; OCT- and OCT+ held together for a second = back to ON. OUTPUT page, SELECT pressed: OFF > ON > CONTROL. */
#define LINK_OK 0x4Cu
enum { LK_CC_MASTER = 7, LK_CC_KNOB1 = 20, LK_CC_SELECT = 24, LK_CC_ALGO = 25, LK_CC_PRESETS = 26, LK_CC_BTN0 = 40, LK_CC_STOP = 54, LK_CC_LEFT = 56, LK_CC_RIGHT = 57, LK_CC_F1 = 60 };
static const uint8_t LK_F_OF[14] = {0, 0, 5, 0, 2, 3, 1, 0, 0, 0, 4, 6, 0, 0};   /* GLO + button = F n (0: none) */
static const uint8_t LK_ENC_CC[7] = {LK_CC_SELECT, LK_CC_ALGO, LK_CC_KNOB1, LK_CC_KNOB1 + 1, LK_CC_KNOB1 + 2, LK_CC_KNOB1 + 3, LK_CC_PRESETS};
static uint32_t lk_glo_used, lk_glo_t, lk_master = 0xFFFFu, lk_keys_prev, lk_exit_t, lk_drawn;
static uint8_t lk_held[27];

static void lk_cc(uint32_t n, uint32_t v) { midi_send(0xB0u, n, v & 127u); }   /* channel 1 */

static int32_t link_enc_take(uint32_t id)       /* fm1_enc_take, and the detents go to the VIBE too (CONTROL: only there) */
{
    int32_t e = fm1_enc_take(id);
    if (e && set.link) { int32_t c = e > 63 ? 63 : e < -63 ? -63 : e; lk_cc(LK_ENC_CC[id % 7u], c > 0 ? (uint32_t)c : (uint32_t)(128 + c)); }
    return set.link == 2u ? 0 : e;
}
static void link_buttons(uint32_t b, uint32_t rel)   /* the buttons pressed / let go since the last call, as the VIBE's */
{
    uint32_t k, layer = fm1_in.buttons >> BTN_SAVE & 1u ? 1u : fm1_in.buttons >> BTN_GLO & 1u ? 2u : 0u;
    for (k = 0; k < 14u; k++) {
        uint32_t pressed = b >> k & 1u, released = rel >> k & 1u;
        if (!pressed && !released) continue;
        if (k == BTN_OCT_DOWN || k == BTN_OCT_UP) { if (layer == 1u) lk_cc(k == BTN_OCT_DOWN ? LK_CC_LEFT : LK_CC_RIGHT, pressed ? 127 : 0); continue; }
        if (k == BTN_GLO) {                     /* held: the F layer; tapped alone: MENU */
            if (pressed) { lk_glo_used = 0; lk_glo_t = fm1_ms; }
            else if (!lk_glo_used && fm1_ms - lk_glo_t < 500u) { lk_cc(LK_CC_BTN0 + BTN_GLO, 127); lk_cc(LK_CC_BTN0 + BTN_GLO, 0); }
            continue;
        }
        if (layer == 2u && LK_F_OF[k]) { lk_glo_used = 1; lk_cc(LK_CC_F1 + LK_F_OF[k] - 1u, pressed ? 127 : 0); continue; }
        if (k == BTN_PLAY && layer == 1u) { lk_cc(LK_CC_STOP, pressed ? 127 : 0); continue; }
        lk_cc(LK_CC_BTN0 + k, pressed ? 127 : 0);
    }
}
static void link_master(void)                   /* MASTER: 0..1023 -> 0..127, sent when it moves */
{
#ifndef OBV_HOST
    int32_t adc = fm1_adc_read(FM1_ADC_MASTER);
    if (adc >= 0) {                             /* (its ADC flickers by one step: sent once it has moved two; the ends always) */
        uint32_t v = (uint32_t)adc >> 3, d = lk_master == 0xFFFFu ? 99u : v > lk_master ? v - lk_master : lk_master - v;
        if (lk_master == 0xFFFFu) lk_master = v;
        else if (d >= 2u || ((v == 0u || v == 127u) && d)) { lk_cc(LK_CC_MASTER, v); lk_master = v; }
    }
#endif
}
static void link_set(uint32_t mode)             /* OFF > ON > CONTROL; CONTROL: nothing sounds here */
{
    set.link = (uint8_t)(mode % 3u); set_dirty = 1; set_t = fm1_ms;
    lk_drawn = 0; lk_exit_t = 0;
    if (set.link == 2u) { uint32_t t; ctl_seq_run = 0; for (t = 0; t < NTRK; t++) parts_off(t); lk_keys_prev = fm1_in.notes; }
    say(set.link == 2u ? "LINK CONTROL: OCT- + OCT+ 1 S = BACK" : set.link ? "VIBE LINK ON: KNOBS, KEYS, BUTTONS GO OUT" : "VIBE LINK OFF");
}
static void link_control(void)                  /* the main loop while CONTROL: the panel is the VIBE's, nothing else runs */
{
    uint32_t keys = fm1_in.notes, down = keys & ~lk_keys_prev, up = lk_keys_prev & ~keys, base = (uint32_t)(KEY0 + 12 * set.oct), k, b, rel;
    lk_keys_prev = keys;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u) { lk_held[k] = (uint8_t)(base + k); midi_send(0x90u | MIDI_OUT_CH, lk_held[k], KEY_VEL(k)); }
        if (up >> k & 1u) midi_send(0x80u | MIDI_OUT_CH, lk_held[k], 0);
    }
    b = fm1_input_edges(&rel);
    {   /* OCT- and OCT+ together, a second: back to ON (alone: the octave, as always) */
        uint32_t both = (fm1_in.buttons >> BTN_OCT_DOWN & 1u) && (fm1_in.buttons >> BTN_OCT_UP & 1u);
        if (both && !lk_exit_t) lk_exit_t = fm1_ms | 1u;
        if (!both) lk_exit_t = 0;
        if (both && fm1_ms - lk_exit_t > 1000u) { link_set(1); keys_prev = fm1_in.notes; lcd_fill(0, 0, 240, 240, C_PANEL); dirty_hdr = dirty_page = dirty_steps = dirty_tags = 1; return; }
        if (!both && !(fm1_in.buttons >> BTN_SAVE & 1u)) {
            if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; set_dirty = 1; set_t = fm1_ms; lk_drawn = 0; }
            if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; set_dirty = 1; set_t = fm1_ms; lk_drawn = 0; }
        }
    }
    link_buttons(b, rel);
    for (k = 0; k < 7u; k++) link_enc_take(k);
    link_master();
    if (set_dirty && fm1_ms - set_t > 2000u) { settings_save(&set, sizeof set); set_dirty = 0; }   /* (the octave, the mode) */
    for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), keys >> k & 1u);
    for (k = 2; k < 14u; k++) fm1_led_key(k, fm1_in.buttons >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, set.oct < 0); fm1_led_key(BTN_OCT_UP, set.oct > 0);
    if (!lk_drawn) {
        char s[16] = "OCTAVE +0";
        lk_drawn = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 24, 240, &FONT_L, "VIBE LINK", C_WHITE, 1);
        draw_text_box(0, 60, 240, &FONT_S, "OberVave as a controller", RGB(140, 140, 140), 1);
        draw_text_box(0, 96, 240, &FONT_S, "knobs, buttons, keys > the VIBE", RGB(200, 200, 200), 1);
        draw_text_box(0, 114, 240, &FONT_S, "SAVE = SHIFT   GLO + key = F1-F6", RGB(200, 200, 200), 1);
        draw_text_box(0, 132, 240, &FONT_S, "GLO tapped = MENU   SEL = ENTER", RGB(200, 200, 200), 1);
        s[7] = set.oct < 0 ? '-' : '+'; s[8] = (char)('0' + (set.oct < 0 ? -set.oct : set.oct));
        draw_text_box(0, 168, 240, &FONT_S, s, RGB(140, 140, 140), 1);
        draw_text_box(0, 206, 240, &FONT_S, "OCT- + OCT+ held 1 s: back", RGB(90, 90, 90), 1);
    }
}
