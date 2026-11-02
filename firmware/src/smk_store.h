/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port of OberVave: Copyright (C) 2026 zvenson */
/* OberVave's flash data on the M-Vave SMK-37 (BOARD_SMK37; platform.c includes this instead of its FM-1 lines).
 * On the FM-1 the settings sit at 0xFC000 and the 8 sequencer patterns at 0xF4000..0xFBFFF: on the SMK that is the
 * protected USR area (stock resources, kept for the way back to stock), so everything moves into the store window
 * FL_DATA 0x9D000..0xBAFFF (the stock VM), as the fm1-to-smk37 kit lays it out:
 *   0x9D000          settings (globals + 32 user programs), the same sector format as on the FM-1
 *   0x9E000-0xA5FFF  sequencer patterns 1-8, one sector each
 *   0xA6000-0xA7FFF  the 99 user programs, 50 a sector
 *   0xAF000          owner tag "SMK37DATA" + OBV_DATA_FAMILY: another firmware's data here is cleared, never parsed
 *   0xC2000-0xE1FFF  (3.0) the patterns of four tracks, 8 slots of 4 sectors, in FL_SMP (USRFLASH: SLOOP's sample
 *                    slots; OberVave had it unused): the old one-sector patterns above are only read to bring them over
 * The rest of FL_DATA stays unused. Calibration (0xC1000), BT pairing, USR and key_mac are never in reach:
 * FL_RANGE_OK below is the app's allow-list, and the RAM flash driver also refuses every protected range on its
 * own (FL_GUARD, fm1_flash_map.h). Macros and compile-time checks only, so the host tests include it. */
#pragma once
#include <stdint.h>

#define SETTINGS_AT 0x0009D000u                 /* one 4 KiB sector */
#define SETTINGS_MAX (4096u - 12u)              /* the sector minus the header (OberVave keeps 32 programs in it) */
#define BLOBS_AT 0x0009E000u                    /* the sequencer patterns: NBLOB sectors */
#define NBLOB 8u
#define PSECT_AT 0x000A6000u                    /* the user programs: NPSECT sectors */
#define NPSECT 2u
#define OWNER_AT 0x000AF000u                    /* the store's owner tag (the kit's place for it) */
#define SLOTS_AT 0x000C2000u                    /* the 3.0 patterns: NBLOB slots of SLOT_SECT sectors in FL_SMP */
#define SLOT_SECT 4u
#ifndef OBV_DATA_FAMILY
#define OBV_DATA_FAMILY "OBERVAV"               /* (tools/build.py sets it: SMK_DATA_FAMILY) */
#endif
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || \
                             FL_IN(off, n, BLOBS_AT, BLOBS_AT + NBLOB * 0x1000u) || \
                             FL_IN(off, n, PSECT_AT, PSECT_AT + NPSECT * 0x1000u) || FL_IN(off, n, OWNER_AT, OWNER_AT + 0x1000u) || \
                             FL_IN(off, n, SLOTS_AT, SLOTS_AT + NBLOB * SLOT_SECT * 0x1000u))
#include "fm1_flash_map.h"

#if !BOARD_SMK37
#error "smk_store.h is the SMK-37 layout (BOARD_SMK37)"
#endif
_Static_assert(SETTINGS_AT >= FL_DATA_LO && SETTINGS_AT + 0x1000u <= BLOBS_AT, "SMK store: settings outside FL_DATA");
_Static_assert(BLOBS_AT + NBLOB * 0x1000u <= OWNER_AT, "SMK store: the patterns run into the owner tag");
_Static_assert(BLOBS_AT + NBLOB * 0x1000u <= PSECT_AT && PSECT_AT + NPSECT * 0x1000u <= OWNER_AT, "SMK store: the programs overlap");
_Static_assert(OWNER_AT + 0x1000u <= FL_DATA_HI, "SMK store: the owner tag outside FL_DATA");
_Static_assert(SLOTS_AT >= FL_SMP_LO && SLOTS_AT + NBLOB * SLOT_SECT * 0x1000u <= FL_SMP_HI, "SMK store: the pattern slots outside FL_SMP");
_Static_assert(FL_WIN_SAFE(SLOTS_AT, SLOTS_AT + NBLOB * SLOT_SECT * 0x1000u), "SMK store: the slots overlap a protected range");
_Static_assert(FL_WIN_SAFE(SETTINGS_AT, OWNER_AT + 0x1000u), "SMK store: overlaps a protected range");
_Static_assert(sizeof OBV_DATA_FAMILY - 1u >= 1u && sizeof OBV_DATA_FAMILY - 1u <= 7u, "SMK store: family 1..7 characters");

/* the 16-byte owner tag: "SMK37DATA" + the family, zero padded (a 7-letter family fills it, no NUL) */
static void smk_owner_tag(uint8_t tag[16])
{
    static const char FAM[] = OBV_DATA_FAMILY;
    uint32_t i;
    for (i = 0; i < 16u; i++)
        tag[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
}
