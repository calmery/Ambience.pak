#!/bin/sh
# NextUI Tool launcher. NextUI runs this when the pak is selected.
DIR="$(dirname "$0")"
cd "$DIR" || exit 1

# Pick the binary for this device.
BIN=""
if [ -n "$PLATFORM" ] && [ -x "./bin/$PLATFORM/ambience" ]; then
    BIN="./bin/$PLATFORM/ambience"
else
    for p in tg5040 tg5050; do
        if [ -x "./bin/$p/ambience" ]; then BIN="./bin/$p/ambience"; break; fi
    done
fi
[ -n "$BIN" ] || { echo "no ambience binary found" > log.txt; exit 1; }

# Allow bundling our own libs under ./lib if ever needed.
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"

exec "$BIN" > log.txt 2>&1
