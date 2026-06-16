#!/usr/bin/env bash
# Cross-compile Ambience for a NextUI device using the official toolchain image.
# Usage: ./build-pak.sh [tg5040|tg5050]   (default tg5040 = TrimUI Brick)
set -euo pipefail

PLATFORM="${1:-tg5040}"
IMG="ghcr.io/loveretro/${PLATFORM}-toolchain:latest"
HERE="$(cd "$(dirname "$0")" && pwd)"

echo ">> building Ambience for ${PLATFORM} using ${IMG}"
docker run --rm --platform linux/amd64 \
  -v "${HERE}":/work -w /work "${IMG}" \
  /bin/bash -lc '
    set -e
    source ~/.bashrc
    OUT="bin/'"${PLATFORM}"'/ambience"
    echo "CROSS_COMPILE=${CROSS_COMPILE}  PREFIX=${PREFIX}"
    CF="-I${PREFIX}/include $(pkg-config --cflags sdl2 2>/dev/null)"
    OBJ=""
    for m in system audio ui config actions main; do
      ${CROSS_COMPILE}gcc -O2 -fomit-frame-pointer ${CF} -DUSE_SDL2 -c src/$m.c -o /tmp/$m.o
      OBJ="$OBJ /tmp/$m.o"
    done
    ${CROSS_COMPILE}gcc -O2 -w -fomit-frame-pointer ${CF} -c src/stb_vorbis.c -o /tmp/stb_vorbis.o
    ${CROSS_COMPILE}gcc -O2 -w -fomit-frame-pointer ${CF} -c src/dr_mp3.c -o /tmp/dr_mp3.o
    ${CROSS_COMPILE}gcc $OBJ /tmp/stb_vorbis.o /tmp/dr_mp3.o -o "${OUT}" \
      -L${PREFIX}/lib -L${PREFIX}/lib/${CROSS_TRIPLE} \
      $(pkg-config --libs sdl2 2>/dev/null || echo -lSDL2) -lSDL2_ttf -lm -lpthread -ldl
    file "${OUT}"
  '
echo ">> done: bin/${PLATFORM}/ambience"
