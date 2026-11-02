/* SPDX-License-Identifier: GPL-3.0-only */
/* The parameter table: a patch is a byte per parameter plus a 10-character name. The UI, MIDI CC, the
 * randomizer and the storage all walk this table. */
enum {
    P_O1_WAVE, P_O1_PW, P_O1_LVL, P_NOISE,
    P_O2_WAVE, P_O2_PW, P_O2_LVL, P_O2_SEMI,
    P_SYNC, P_XMOD, P_ENV_O2, P_O2_FINE,
    P_CUTOFF, P_RESO, P_FENV_AMT, P_KEYTRK, P_FTYPE,
    P_DRIVE, P_VEL_FLT, P_VEL_AMP,
    P_FA, P_FD, P_FS, P_FR,
    P_AA, P_AD, P_AS, P_AR,
    P_LFO_SHAPE, P_LFO_RATE, P_LFO_DELAY, P_LFO_PHASE,
    P_LFO_PITCH, P_LFO_PWM, P_LFO_FLT, P_LFO_AMP, P_LFO_PITCH_TO, P_LFO_PWM_TO,
    P_MODE, P_UNI_N, P_UNI_DET, P_GLIDE,
    P_VINTAGE, P_SPREAD, P_BEND, P_VOLUME,
    P_MOD_DEPTH,
    P_ARP_MODE, P_ARP_DIV, P_ARP_OCT, P_ARP_GATE,
    P_FMORPH,                                   /* (appended: the MIDI CC numbers of the above stay) */
    P_CHO_RATE, P_CHO_DEPTH, P_CHO_MIX,
    P_DLY_TIME, P_DLY_FB, P_DLY_TONE, P_DLY_MIX,
    P_REV_SIZE, P_REV_DAMP, P_REV_PRE, P_REV_MIX,
    P_POLY_N,                                   /* (appended: the MIDI CC numbers of the above stay) */
    NPARAM
};
#define PATCH_NAME 14u                          /* DØG-5: widened 10 -> 14 */
#define PATCH_BYTES 96u                         /* DØG-5: grown 80 -> 96 (the "DOG5" program format) */
#define PATCH_NAME_AT 80u                       /* the name at a FIXED offset, decoupled from NPARAM: bytes 0-79 are
                                                 * parameters (63 used; room for the Psych Chain GROWL/ALLEY/DRAIN/CHEW,
                                                 * P_POLY_N and v1.1), the 14-char name 80-93, 2 spare (94-95). Adding a
                                                 * parameter (always at the end of the table) never moves the name. */
#if NPARAM > PATCH_NAME_AT
#error "the parameters run into the name field"
#endif
#if PATCH_NAME_AT + PATCH_NAME > PATCH_BYTES
#error "the name + spare outgrew PATCH_BYTES"
#endif
#define NPROG 99u                               /* 1-32 the DØG-5 factory, 33-99 INIT; the user's saves over them */
#define NFACT 32u                               /* the 32 DØG-5 patches (dog5-docs/DOG-5-presets.md); the rest INIT */

enum { K_NUM, K_BIP, K_ONOFF, K_WAVE, K_FTYPE, K_MODE, K_LSHAPE, K_LPHASE, K_TO, K_ARP, K_DIV, K_SEMI, K_MS, K_HZ,
       K_PCT, K_CUT, K_OCT, K_MORPH, K_DDIV };
enum { W_OFF, W_SAW, W_PLS, W_BOTH };
enum { F_LP12, F_HP12, F_BP12, F_NOTCH, F_LP24, F_SEM, NFTYPE };   /* SEM: LP .. notch .. HP by MORPH */
enum { M_POLY, M_DUAL, M_UNISON, M_MONO };
enum { L_TRI, L_SAW, L_RAMP, L_SQR, L_SH, L_RND };
enum { LP_SYNC, LP_FREE, LP_SPREAD };
enum { TO_O1, TO_O2, TO_BOTH };
enum { A_OFF, A_UP, A_DOWN, A_UPDN, A_RND, A_PLAY, NARPMODE };
enum { D_4, D_8, D_8T, D_16, D_16T, D_32, D_32T, NDIV };
enum { DD_16, DD_8T, DD_8, DD_8D, DD_4, DD_4D, DD_2, NDDIV };   /* the delay's note values */

typedef struct {
    const char *name;                           /* 8 characters at most (the knob label) */
    uint8_t max, def, kind, cc;                 /* cc: a fixed MIDI CC, 0 = taken from 20 up in table order */
} pdef_t;
static const pdef_t PDEF[NPARAM] = {
    [P_O1_WAVE] = {"WAVE", 3, W_SAW, K_WAVE, 0},
    [P_O1_PW] = {"PW", 127, 64, K_PCT, 0},
    [P_O1_LVL] = {"LEVEL", 127, 110, K_NUM, 0},
    [P_NOISE] = {"NOISE", 127, 0, K_NUM, 0},
    [P_O2_WAVE] = {"WAVE", 3, W_SAW, K_WAVE, 0},
    [P_O2_PW] = {"PW", 127, 64, K_PCT, 0},
    [P_O2_LVL] = {"LEVEL", 127, 100, K_NUM, 0},
    [P_O2_SEMI] = {"SEMI", 48, 24, K_SEMI, 0},
    [P_SYNC] = {"SYNC", 1, 0, K_ONOFF, 0},
    [P_XMOD] = {"X-MOD", 127, 0, K_NUM, 0},
    [P_ENV_O2] = {"ENV>O2", 127, 64, K_BIP, 0},
    [P_O2_FINE] = {"FINE", 127, 70, K_BIP, 0},
    [P_CUTOFF] = {"CUTOFF", 127, 80, K_CUT, 74},
    [P_RESO] = {"RESO", 127, 20, K_NUM, 71},
    [P_FENV_AMT] = {"ENV AMT", 127, 90, K_BIP, 0},
    [P_KEYTRK] = {"KEY TRK", 127, 64, K_PCT, 0},
    [P_FTYPE] = {"TYPE", NFTYPE - 1, F_LP24, K_FTYPE, 0},
    [P_DRIVE] = {"DRIVE", 127, 0, K_NUM, 0},
    [P_VEL_FLT] = {"VEL>FLT", 127, 40, K_NUM, 0},
    [P_VEL_AMP] = {"VEL>AMP", 127, 60, K_NUM, 0},
    [P_FA] = {"ATTACK", 127, 20, K_MS, 0},
    [P_FD] = {"DECAY", 127, 70, K_MS, 0},
    [P_FS] = {"SUSTAIN", 127, 40, K_NUM, 0},
    [P_FR] = {"RELEASE", 127, 60, K_MS, 0},
    [P_AA] = {"ATTACK", 127, 10, K_MS, 73},
    [P_AD] = {"DECAY", 127, 70, K_MS, 0},
    [P_AS] = {"SUSTAIN", 127, 100, K_NUM, 0},
    [P_AR] = {"RELEASE", 127, 60, K_MS, 72},
    [P_LFO_SHAPE] = {"SHAPE", 5, L_TRI, K_LSHAPE, 0},
    [P_LFO_RATE] = {"RATE", 127, 70, K_HZ, 0},
    [P_LFO_DELAY] = {"DELAY", 127, 0, K_NUM, 0},
    [P_LFO_PHASE] = {"PHASE", 2, LP_FREE, K_LPHASE, 0},
    [P_LFO_PITCH] = {"PITCH", 127, 0, K_NUM, 0},
    [P_LFO_PWM] = {"PWM", 127, 0, K_NUM, 0},
    [P_LFO_FLT] = {"FILTER", 127, 0, K_NUM, 0},
    [P_LFO_AMP] = {"AMP", 127, 0, K_NUM, 0},
    [P_LFO_PITCH_TO] = {"PITCH>", 2, TO_BOTH, K_TO, 0},
    [P_LFO_PWM_TO] = {"PWM>", 2, TO_BOTH, K_TO, 0},
    [P_MODE] = {"MODE", 3, M_POLY, K_MODE, 0},
    [P_UNI_N] = {"VOICES", 8, 4, K_NUM, 0},
    [P_UNI_DET] = {"DETUNE", 127, 30, K_NUM, 0},
    [P_GLIDE] = {"GLIDE", 127, 0, K_MS, 5},
    [P_VINTAGE] = {"VINTAGE", 127, 30, K_NUM, 0},
    [P_SPREAD] = {"SPREAD", 127, 60, K_NUM, 0},
    [P_BEND] = {"BEND", 24, 2, K_NUM, 0},
    [P_VOLUME] = {"VOLUME", 127, 100, K_NUM, 0},
    [P_MOD_DEPTH] = {"MOD DPT", 127, 0, K_NUM, 1},
    [P_ARP_MODE] = {"ARP", 5, A_OFF, K_ARP, 0},
    [P_ARP_DIV] = {"DIV", 6, D_16, K_DIV, 0},
    [P_ARP_OCT] = {"OCTAVES", 4, 1, K_OCT, 0},
    [P_ARP_GATE] = {"GATE", 127, 64, K_PCT, 0},
    [P_FMORPH] = {"MORPH", 127, 0, K_MORPH, 0},
    [P_CHO_RATE] = {"RATE", 127, 40, K_NUM, 0},
    [P_CHO_DEPTH] = {"DEPTH", 127, 50, K_NUM, 0},
    [P_CHO_MIX] = {"MIX", 127, 0, K_NUM, 0},
    [P_DLY_TIME] = {"TIME", NDDIV - 1, DD_8D, K_DDIV, 0},
    [P_DLY_FB] = {"FEEDBK", 127, 50, K_NUM, 0},
    [P_DLY_TONE] = {"TONE", 127, 70, K_NUM, 0},
    [P_DLY_MIX] = {"MIX", 127, 0, K_NUM, 0},
    [P_REV_SIZE] = {"SIZE", 127, 80, K_NUM, 0},
    [P_REV_DAMP] = {"DAMP", 127, 60, K_NUM, 0},
    [P_REV_PRE] = {"PRE-DLY", 127, 20, K_NUM, 0},
    [P_REV_MIX] = {"MIX", 127, 0, K_NUM, 0},
    [P_POLY_N] = {"POLY", 8, 5, K_NUM, 0},      /* 1-8, default 5: the POLY/DUAL voice window (obv_engine.h poly_n) */
};

static const char *const WAVE_NAME[4] = {"OFF", "SAW", "PULSE", "SAW+PLS"};
static const char *const FTYPE_NAME[NFTYPE] = {"LP 12", "HP 12", "BP 12", "NOTCH", "LP 24", "SEM"};
static const char *const MODE_NAME[4] = {"POLY", "DUAL", "UNISON", "MONO"};
static const char *const LSHAPE_NAME[6] = {"TRI", "SAW", "RAMP", "SQUARE", "S&H", "RANDOM"};
static const char *const LPHASE_NAME[3] = {"SYNC", "FREE", "SPREAD"};
static const char *const TO_NAME[3] = {"OSC1", "OSC2", "BOTH"};
static const char *const ARP_NAME[NARPMODE] = {"OFF", "UP", "DOWN", "UP/DN", "RANDOM", "PLAYED"};
static const char *const DIV_NAME[NDIV] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const uint8_t DIV_PER_QUARTER[NDIV] = {1, 2, 3, 4, 6, 8, 12};
static const char *const DDIV_NAME[NDDIV] = {"1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2"};
static const uint8_t DDIV_NUM[NDDIV] = {1, 1, 1, 3, 1, 3, 2}, DDIV_DEN[NDDIV] = {4, 3, 2, 4, 1, 2, 1};   /* of a quarter */

/* the pages: four knobs each (-1 = no knob). DØG-5 has 20 (CLAUDE.md §5); a button's pages are contiguous, so a
 * family is "first page + count" (FAM_* below). DRIVE, TAPE and VOICE + have no knobs yet: their parameters
 * (preamp drive, tape degrade) arrive with the Psych Chain, which changes the patch and the DSP. VOICE + now
 * carries P_POLY_N (the polyphony / voice window). */
typedef struct { const char *title; int8_t p[4]; } page_t;
enum { PG_PERFORM,                                                              /* HOME */
       PG_OSC1, PG_OSC2, PG_OSCMOD, PG_FILTER, PG_FILTER2,                      /* EDIT */
       PG_FENV, PG_AENV,                                                        /* ENV */
       PG_LFO, PG_LFODEST, PG_LFO2,                                             /* LFO */
       PG_DRIVE, PG_BBD, PG_SPRING, PG_TAPE,                                    /* FX: the Psych Chain */
       PG_ARP,                                                                  /* ARP */
       PG_GLOBAL, PG_VOICE, PG_VOICE2, PG_OUTPUT,                               /* GLO */
       NPAGE };
static const page_t PAGE[NPAGE] = {
    [PG_PERFORM] = {"HOME", {P_CUTOFF, P_RESO, P_FENV_AMT, P_AR}},
    [PG_OSC1] = {"OSC 1", {P_O1_WAVE, P_O1_PW, P_O1_LVL, P_NOISE}},
    [PG_OSC2] = {"OSC 2", {P_O2_WAVE, P_O2_PW, P_O2_LVL, P_O2_SEMI}},
    [PG_OSCMOD] = {"OSC MOD", {P_SYNC, P_XMOD, P_ENV_O2, P_O2_FINE}},
    [PG_FILTER] = {"FILTER", {P_RESO, P_FENV_AMT, P_KEYTRK, P_FTYPE}},
    [PG_FILTER2] = {"FILTER +", {P_DRIVE, P_VEL_FLT, P_VEL_AMP, P_FMORPH}},
    [PG_FENV] = {"FILTER ENV", {P_FA, P_FD, P_FS, P_FR}},
    [PG_AENV] = {"AMP ENV", {P_AA, P_AD, P_AS, P_AR}},
    [PG_LFO] = {"LFO", {P_LFO_SHAPE, P_LFO_RATE, P_LFO_DELAY, P_LFO_PHASE}},
    [PG_LFODEST] = {"LFO DEST", {P_LFO_PITCH, P_LFO_PWM, P_LFO_FLT, P_LFO_AMP}},
    [PG_LFO2] = {"LFO +", {P_LFO_PITCH_TO, P_LFO_PWM_TO, -1, -1}},
    [PG_DRIVE] = {"DRIVE", {-1, -1, -1, -1}},                                   /* preamp drive: Psych Chain, not built */
    [PG_BBD] = {"BBD DELAY", {P_DLY_TIME, P_DLY_FB, P_DLY_TONE, P_DLY_MIX}},
    [PG_SPRING] = {"SPRING", {P_REV_SIZE, P_REV_DAMP, P_REV_PRE, P_REV_MIX}},
    [PG_TAPE] = {"TAPE", {-1, -1, -1, -1}},                                     /* tape degrade: Psych Chain, not built */
    [PG_ARP] = {"ARP", {P_ARP_MODE, P_ARP_DIV, P_ARP_OCT, P_ARP_GATE}},
    [PG_GLOBAL] = {"GLOBAL", {-1, -1, -1, -1}},   /* tempo, MIDI channel, tune, CPU: globals, drawn by hand */
    [PG_VOICE] = {"VOICE", {P_MODE, P_UNI_N, P_UNI_DET, P_GLIDE}},
    [PG_VOICE2] = {"VOICE +", {P_POLY_N, -1, -1, -1}},                          /* polyphony: the voice window 1-8 */
    [PG_OUTPUT] = {"OUTPUT", {P_VINTAGE, P_SPREAD, P_BEND, P_VOLUME}},
};
_Static_assert(NPAGE == 20, "DOG-5 has 20 pages (CLAUDE.md section 5)");

/* the button families: each remembers its last page (fam_last in obervave.c) */
enum { FAM_HOME, FAM_EDIT, FAM_ENV, FAM_LFO, FAM_FX, FAM_ARP, FAM_GLO, NFAM };
static const struct { uint8_t first, count; } FAM[NFAM] = {
    [FAM_HOME] = {PG_PERFORM, 1}, [FAM_EDIT] = {PG_OSC1, 5}, [FAM_ENV] = {PG_FENV, 2}, [FAM_LFO] = {PG_LFO, 3},
    [FAM_FX] = {PG_DRIVE, 4}, [FAM_ARP] = {PG_ARP, 1}, [FAM_GLO] = {PG_GLOBAL, 4},
};

typedef struct { uint8_t v[PATCH_BYTES]; } patch_t;   /* v[0..79] parameters, v[80..93] name, v[94..95] spare */

static void patch_defaults(patch_t *p)
{
    uint32_t i;
    for (i = 0; i < NPARAM; i++) p->v[i] = PDEF[i].def;
    for (; i < PATCH_BYTES; i++) p->v[i] = 0;                        /* unused param space (63-79) + spare (94-95) */
    for (i = 0; i < PATCH_NAME; i++) p->v[PATCH_NAME_AT + i] = ' ';  /* the name field: space-filled (short names blank, not '?') */
}

/* a factory patch: a name and a list of (parameter, value) pairs over the defaults, ended by 255 */
typedef struct { const char *name; const uint8_t *set; } factory_t;
static void patch_from_factory(patch_t *p, const factory_t *f)
{
    const uint8_t *s = f->set;
    uint32_t i;
    patch_defaults(p);
    while (s[0] != 255u) { p->v[s[0]] = s[1]; s += 2; }
    for (i = 0; i < PATCH_NAME && f->name[i]; i++) p->v[PATCH_NAME_AT + i] = (uint8_t)f->name[i];
}
