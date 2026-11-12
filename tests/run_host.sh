#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/obervave.c on this computer (tests/host.c): build/host/obervave.wav and the screens as PNG.
#   BOARD=smk37 tests/run_host.sh [script]: the SMK-37 build (pads, faders, wheels, outer keys; build/host/obervave-smk37*)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
N=obervave; D=
if [ "${BOARD:-fm1}" = smk37 ]; then N=obervave-smk37; D=-DBOARD_SMK37=1; fi
cc -O2 -Wall -Wno-unused-function -DOBV_HOST $D -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/$N firmware/src/obervave.c
build/host/$N build/host/$N ${1:+"$1"}
"${PYTHON:-python3}" - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("obervave-*.ppm")):   # (the SMK build's too: obervave-smk37-*.ppm)
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
