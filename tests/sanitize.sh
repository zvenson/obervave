#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The app on the host under AddressSanitizer + UndefinedBehaviorSanitizer (every check, a negative shift too), both
# boards, every script: any report fails. Linux (gcc or clang); run from anywhere.
set -e
cd "$(dirname "$0")/.."
OUT=build/host/san
mkdir -p "$OUT"
fail=0
for B in fm1 smk37; do
    D=; [ $B = smk37 ] && D=-DBOARD_SMK37=1
    ${CC:-cc} -g -O1 -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
        -DOBV_HOST $D -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o "$OUT/$B" firmware/src/obervave.c
    for t in "" tests/*.txt tests/smk/*.txt; do
        n=$(basename "${t:-default}" .txt)
        if ! ASAN_OPTIONS=detect_leaks=0 "$OUT/$B" "$OUT/$B-$n" $t >"$OUT/$B-$n.log" 2>&1 \
           || grep -qE "runtime error|AddressSanitizer" "$OUT/$B-$n.log"; then
            echo "FAIL $B $n:"; grep -m3 -E "runtime error|AddressSanitizer" "$OUT/$B-$n.log"; fail=1
        fi
    done
done
[ $fail = 0 ] && echo "sanitizers: every script clean on both boards"
exit $fail
