/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port of OberVave: Copyright (C) 2026 zvenson
 * (after genkigenki/sloop-fm1 smk37 tests/smk_flash_test.c) */
/* OberVave's SMK-37 flash map (build with -DBOARD_SMK37=1; firmware/src/smk_store.h, hal/fm1_flash_map.h): every
 * 4 KiB sector of the 1 MiB part through the app's allow-list (FL_RANGE_OK / FL_GUARD) and the update loader's
 * (FL_LDR_RANGE_OK): none may ever reach the head 0x0-0x3FFF, BTIF / USRTRIM 0xC0000-0xC1FFF (the calibration),
 * USR 0xF4000-0xFDFFF or key_mac 0xFF000-. OberVave's objects (settings, 8 patterns, the owner tag) and the OTA
 * staging lie in the store window, each in its own sector; ranges that wrap or straddle a boundary are refused. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "../firmware/src/smk_store.h"
#define LDR_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)       /* what loader.c uses on the SMK */

static int bad;
static void check(const char *what, int ok) { printf("smk flash: %-86s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int protected_sector(uint32_t s)
{
    return s < 0x4000u || (s >= 0xC0000u && s < 0xC2000u) || (s >= 0xF4000u && s < 0xFE000u) || s >= 0xFF000u;
}

int main(void)
{
    uint32_t s, i, leak_app = 0, leak_ldr = 0, leak_guard = 0, allowed = 0, objs[NBLOB + 2], distinct = 1, inside = 1;
    uint8_t tag[16];
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        if (!protected_sector(s))
            continue;
        leak_app += FL_RANGE_OK(s, 0x1000u) || FL_GUARD(s, 0x1000u) || FL_RANGE_OK(s, 256u) || FL_RANGE_OK(s + 0xF00u, 256u);
        leak_ldr += LDR_RANGE_OK(s, 0x1000u) || LDR_RANGE_OK(s + 0xF00u, 256u);
        leak_guard += !FL_PROTECTED(s, 0x1000u) || !FL_PROTECTED(s + 0xF00u, 256u);
    }
    for (s = 0; s < 0x100000u; s += 0x1000u)
        allowed += FL_GUARD(s, 0x1000u);
    check("no protected sector passes the app's allow-list (erase, page)", leak_app == 0);
    check("no protected sector passes the loader's allow-list", leak_ldr == 0);
    check("the RAM driver's own net (FL_PROTECTED) covers every protected sector", leak_guard == 0);
    check("the app may write 49 sectors: settings, 8 patterns (1.5), 2 of programs, the owner tag, 5 of OTA, 8 slots of 4", allowed == 17u + NBLOB * SLOT_SECT);
    check("straddling ranges refused: 0xBF000+0x2000, 0x9C000+0x2000, 0xA5000+0x2000, 0x3000+0x2000",
          !FL_GUARD(0xBF000u, 0x2000u) && !FL_GUARD(0x9C000u, 0x2000u) && !FL_GUARD(0xA5000u, 0x2000u) &&
          !LDR_RANGE_OK(0xBF000u, 0x2000u) && !LDR_RANGE_OK(0x3000u, 0x2000u) && FL_PROTECTED(0xBF000u, 0x2000u));
    check("wrapping ranges refused (off + n past 4 GiB)",
          !FL_GUARD(0xBB000u, 0xFFFFF000u) && !FL_GUARD(SETTINGS_AT, 0xFFFFFF00u) && !LDR_RANGE_OK(0x9D000u, 0xFFFFFF00u) &&
          FL_PROTECTED(0xBB000u, 0xFFFFF000u));
    objs[0] = SETTINGS_AT;
    for (i = 0; i < NBLOB; i++) objs[1 + i] = BLOBS_AT + i * 0x1000u;
    objs[NBLOB + 1] = OWNER_AT;
    for (i = 0; i < NBLOB + 2u; i++) {
        uint32_t j;
        inside &= FL_GUARD(objs[i], 0x1000u) && FL_GUARD(objs[i] + 0xF00u, 0x100u) && objs[i] >= FL_DATA_LO &&
                  objs[i] + 0x1000u <= FL_DATA_HI && objs[i] % 0x1000u == 0u;
        for (j = 0; j < NBLOB + 2u; j++) if (i != j && objs[i] == objs[j]) distinct = 0;
    }
    check("settings 0x9D000, patterns 0x9E000-0xA5FFF, owner tag 0xAF000: in FL_DATA, writable, apart",
          inside && distinct && SETTINGS_AT == 0x9D000u && BLOBS_AT == 0x9E000u && OWNER_AT == 0xAF000u);
    check("OTA staging 0xBB000-0xBFFFF writable, the window ends below BTIF",
          FL_GUARD(0xBB000u, 0x1000u) && FL_GUARD(0xBFF00u, 0x100u) && !FL_GUARD(0xC0000u, 0x100u) && FL_OTA_HI == 0xC0000u);
    check("the FM-1's places refused on the SMK: settings 0xFC000, patterns 0xF4000-0xFBFFF",
          !FL_GUARD(0xFC000u, 0x1000u) && !FL_GUARD(0xF4000u, 0x1000u) && !FL_GUARD(0xFB000u, 0x1000u));
    check("the calibration record 0xC1000 (trimdat at +0x224) never writable",
          !FL_GUARD(0xC1000u, 0x1000u) && !FL_GUARD(0xC1200u, 0x100u) && FL_PROTECTED(0xC1224u, 8u));
    check("loader: app area 0x4000-0x9CFFF, records 0x9D000-0xBFFFF only",
          LDR_RANGE_OK(0x4000u, 0x1000u) && LDR_RANGE_OK(0x9C000u, 0x1000u) && LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !LDR_RANGE_OK(0xC0000u, 0x1000u) && FL_REC_HI == 0xC0000u);
    check("FL_PLAIN_LO = the VM start 0x9D000", FL_PLAIN_LO == 0x9D000u);
    check("FL_JEDEC_OK: the SMK part 0xEB6014 and 0x856014 only", FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) &&
          !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0xEB6015u));
    smk_owner_tag(tag);
    check("owner tag: \"SMK37DATA\" + the family, 16 bytes (" OBV_DATA_FAMILY ")",
          !memcmp(tag, "SMK37DATA", 9) && !memcmp(tag + 9, OBV_DATA_FAMILY, sizeof OBV_DATA_FAMILY - 1u) &&
          (sizeof OBV_DATA_FAMILY - 1u == 7u || tag[9 + sizeof OBV_DATA_FAMILY - 1u] == 0u));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
