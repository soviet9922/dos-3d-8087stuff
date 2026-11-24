#!/bin/sh
# Regenerate tables/models and assemble FLIGHT87.COM
#   ./build.sh            normal build
#   ./build.sh -DCOUNT    show 8087 instructions per frame on the HUD
#   ./build.sh -DNOOBS    no obstacles (debug)
set -e
cd "$(dirname "$0")"
python3 tools/gen_data.py
cd src && nasm -f bin "$@" -o ../FLIGHT87.COM flight.asm
ls -l ../FLIGHT87.COM
