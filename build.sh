#!/bin/sh
# Builds ABSvita inside Docker (VitaSDK + libcurl/mbedTLS, see docker/Dockerfile).
#   ./build.sh          -> build/ABSvita.vpk
#   ./build.sh clean
set -e
cd "$(dirname "$0")"
if [ "$1" = clean ]; then rm -rf build; exit 0; fi
if ! docker image inspect absvita-build >/dev/null 2>&1; then
    docker build -t absvita-build docker
fi
exec docker run --rm -v "$PWD":/src -w /src absvita-build sh -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${APP_VERSION:+-DAPP_VERSION=$APP_VERSION} >/dev/null && cmake --build build -j"$(nproc)"'
