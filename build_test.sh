#!/bin/sh
# Emulator test build with the scripted-input autopilot -> build-test/ABSvita.vpk
set -e
cd "$(dirname "$0")"
exec docker run --rm -v "$PWD":/src -w /src absvita-build sh -c \
    'cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release -DAUTOPILOT=ON >/dev/null && cmake --build build-test -j"$(nproc)"'
