#!/bin/sh
# Builds ABSvita inside the vitasdk/vitasdk Docker image.
#   ./build.sh          -> build/ABSvita.vpk
#   ./build.sh clean
set -e
cd "$(dirname "$0")"
if [ "$1" = clean ]; then rm -rf build; exit 0; fi
exec docker run --rm -v "$PWD":/src -w /src vitasdk/vitasdk:latest sh -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j"$(nproc)"'
