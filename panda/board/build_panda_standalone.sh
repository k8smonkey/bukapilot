#!/usr/bin/env bash
# Standalone panda firmware build, replicating panda/board/SConscript's
# "panda" (STM32F413, cortex-m4) project build+sign steps directly with
# arm-none-eabi-gcc, bypassing the monolithic root SConstruct (which
# unconditionally parses every openpilot subsystem and needs the full
# Qt/OpenCL/ffmpeg dev environment just to parse, even though none of
# that is needed to build this one firmware).
set -euo pipefail

PREFIX=arm-none-eabi-
CC=${PREFIX}gcc
OBJCOPY=${PREFIX}objcopy

GIT_SHA=$(git rev-parse --short=8 HEAD)
VERSION="DEV-${GIT_SHA}-DEBUG"

mkdir -p obj
echo "const uint8_t gitversion[] = \"${VERSION}\";" > obj/gitversion.h
printf '%s' "${VERSION}" > obj/version

# obj/cert.h (RSAPublicKey structs for debug + release pubkeys)
python3 - <<'PYEOF'
from Crypto.PublicKey import RSA

def to_c_uint32(x):
    nums = []
    for _ in range(0x20):
        nums.append(x % (2**32))
        x //= (2**32)
    return "{" + 'U,'.join(map(str, nums)) + "U}"

def get_key_header(name):
    rsa = RSA.importKey(open(f'../certs/{name}.pub').read())
    assert rsa.size_in_bits() == 1024
    rr = pow(2**1024, 2, rsa.n)
    n0inv = 2**32 - pow(rsa.n, -1, 2**32)
    return [
        f"RSAPublicKey {name}_rsa_key = {{",
        f"  .len = 0x20,",
        f"  .n0inv = {n0inv}U,",
        f"  .n = {to_c_uint32(rsa.n)},",
        f"  .rr = {to_c_uint32(rr)},",
        f"  .exponent = {rsa.e},",
        f"}};",
    ]

with open("obj/cert.h", "w") as f:
    for name in ["debug", "release"]:
        f.write("\n".join(get_key_header(name)) + "\n")
PYEOF

LINKERSCRIPT="$(pwd)/stm32fx/stm32fx_flash.ld"
APP_START_ADDRESS=0x8004000
CERT_FN="$(pwd)/../certs/debug"

FLAGS=(
  -Wall -Wextra -Wstrict-prototypes -Werror
  -mlittle-endian -mthumb -nostdlib -fno-builtin
  -T"${LINKERSCRIPT}"
  -std=gnu11
  -mcpu=cortex-m4 -mhard-float -DSTM32F4 -DSTM32F413xx -mfpu=fpv4-sp-d16
  -fsingle-precision-constant -Os -g -DPANDA
  -DALLOW_DEBUG
)

INCLUDES=(-Istm32fx/inc -Istm32h7/inc -I.. -I.)

echo "== compiling startup =="
${CC} "${FLAGS[@]}" "${INCLUDES[@]}" -c stm32fx/startup_stm32f413xx.s -o obj/startup-panda.o

echo "== bootstub =="
${CC} "${FLAGS[@]}" "${INCLUDES[@]}" -c ../crypto/rsa.c -o obj/rsa-panda.o
${CC} "${FLAGS[@]}" "${INCLUDES[@]}" -c ../crypto/sha.c -o obj/sha-panda.o
${CC} "${FLAGS[@]}" "${INCLUDES[@]}" -c bootstub.c -o obj/bootstub-panda.o
${CC} "${FLAGS[@]}" -o obj/bootstub.panda.elf obj/startup-panda.o obj/rsa-panda.o obj/sha-panda.o obj/bootstub-panda.o
${OBJCOPY} -O binary obj/bootstub.panda.elf obj/bootstub.panda.bin

echo "== main =="
${CC} "${FLAGS[@]}" "${INCLUDES[@]}" -c main.c -o obj/main-panda.o
${CC} "${FLAGS[@]}" -Wl,--section-start,.isr_vector=${APP_START_ADDRESS} -o obj/panda.elf obj/startup-panda.o obj/main-panda.o
${OBJCOPY} -O binary obj/panda.elf obj/panda.bin

echo "== signing =="
SETLEN=1 python3 ../crypto/sign.py obj/panda.bin obj/panda.bin.signed "${CERT_FN}"

echo "== done =="
ls -la obj/panda.bin obj/panda.bin.signed obj/panda.elf obj/bootstub.panda.bin obj/bootstub.panda.elf
