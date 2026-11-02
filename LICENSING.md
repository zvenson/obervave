# Licensing

OberVave is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `midi_uart.c` (the TRS MIDI IN; its fix from Felucca Salt by ChanceTheMaker, found by keremimo), `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The platform's wiring `platform.c`, the host mode `tests/`, the docs, the build (`build.sh`, `tools/build.py`) | zvenson, as [Dinghy](https://github.com/zvenson/dinghy) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| The synth: `firmware/src/obervave.c`, `obv_*.h`, `firmware/gen/obv_tables.h` and `tools/gen_tables.py`, the programs, the screen, the video (`video/`) | Copyright (C) 2026 zvenson |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit), on the platform above |
| The browser simulator: `web/sim/` (`sim.c`, `worklet.js`, `sim.js`, `index.html`, `sim.css`, `build.sh`), its build in `docs/sim/`, `tests/sim_test.mjs` | Copyright (C) 2026 zvenson; the pattern (the firmware's sources in an AudioWorklet, the device's clock run by the audio) follows the web simulators of [Makkuro37](https://github.com/genkigenki/Makkuro37) (M. Geisler) and [winampFM](https://github.com/hericdk/winampFM) (hericdk), both GPL-3.0 |
| OberVave on the SMK-37: `firmware/src/obv_smk.h`, `smk_store.h`, the SMK-37 parts of `platform.c`, `obervave.c`, `obv_engine.h`, the pad event queue in `smk_input.h`, `tests/smk_ui_test.py`, `tests/smk/`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, `SMK37.md`, `smk37-port.yaml` | Copyright (C) 2026 zvenson |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| The screen mirror: `firmware/src/screen_mirror/` (`screen_mirror.c`, `.h`), Copyright (c) 2026 Patrick F. Kellogg – OberVave's screen to his [FM-1 Mirror](https://github.com/pfkellogg/fm1-mirror) app; the glue `obv_mirror.h` and the hooks in `lcd.c` are zvenson's | MIT | `firmware/src/screen_mirror/LICENSE` |
| URW Bookman Demi and Inter SemiBold, as outlines in `assets/logo*.svg` (made by `tools/make_logo.py`) | URW base35 licence (AGPL-3.0 with the font-embedding exception), SIL OFL 1.1 | <https://github.com/ArtifexSoftware/urw-base35-fonts>, <https://rsms.me/inter/> |
| Zig (`zig cc`, MIT) and Binaryen's `wasm-opt` (Asyncify, Apache-2.0) build `docs/sim/obervave.wasm`; neither is in this tree | MIT, Apache-2.0 | <https://ziglang.org>, <https://github.com/WebAssembly/binaryen> |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE", "FM-1", "SMK-37", "MKE-P37" and
"Starrykey" belong to their owners. OberVave is inspired by the classic 8-voice poly synths of 1979-84; "Oberheim", "OB-X" and "OB-8" are
trademarks of their owner. OberVave is not made or endorsed by any of them, and none of these marks appear in the
firmware itself.
