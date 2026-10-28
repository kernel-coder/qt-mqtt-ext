#!/usr/bin/env bash
# Builds qt-mqtt-ext and runs every test inside an Ubuntu 24.04 container (Qt 6.4, OpenSSL 3),
# against the broker started with `docker compose -f broker/compose.yaml up -d`.
# Usage: scripts/test-linux.sh [linux/amd64 | linux/arm64 | linux/arm/v7]
set -euo pipefail

platform="${1:-linux/amd64}"
root="$(cd "$(dirname "$0")/.." && pwd)"
if command -v cygpath >/dev/null 2>&1; then
    root="$(cygpath -m "$root")"
fi

MSYS_NO_PATHCONV=1 docker run --rm --platform "$platform" --network qt-mqtt-ext-broker_default \
    -v "$root:/src:ro" \
    -e MQTT_EXT_TEST_HOST=mosquitto \
    -e MQTT_EXT_TEST_TCP_PORT=1883 \
    -e MQTT_EXT_TEST_TLS_PORT=8883 \
    -e MQTT_EXT_TEST_WS_PORT=9001 \
    -e MQTT_EXT_TEST_WSS_PORT=9443 \
    -e MQTT_EXT_TEST_SECRETS_DIR=/src/broker/.secrets \
    ubuntu:24.04 bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends \
            ca-certificates cmake ninja-build g++ qt6-base-dev libssl-dev >/dev/null
        echo "Testing on $(uname -m) with Qt $(qmake6 -query QT_VERSION)"
        cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
        cmake --build /tmp/build
        ctest --test-dir /tmp/build --output-on-failure
    '
