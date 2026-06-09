#!/usr/bin/env bash
# =============================================================================
#  build.sh - NeuralStage (macOS arm64 + Linux x86_64)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/Builds"
CONFIG="${CONFIG:-Release}"
OS="$(uname -s)"

if [ "$OS" = "Darwin" ]; then
    # macOS: prefer native arm64 cmake (avoid anaconda's x86_64 build).
    CMAKE_BIN="${CMAKE_BIN:-/opt/homebrew/bin/cmake}"
    if [ ! -x "$CMAKE_BIN" ]; then
        echo "ERROR: native cmake not found at $CMAKE_BIN. brew install cmake" >&2
        exit 1
    fi
    JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

    "$CMAKE_BIN" -S "$ROOT" -B "$BUILD_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DCMAKE_OSX_ARCHITECTURES=arm64

    "$CMAKE_BIN" --build "$BUILD_DIR" --config "$CONFIG" -j "$JOBS"

    APP="$BUILD_DIR/NeuralStage_artefacts/$CONFIG/NeuralStage.app"
    if [ -d "$APP" ]; then
        echo ""
        echo "Built: $APP"
        if [ "${LAUNCH:-1}" = "1" ]; then
            open "$APP"
        fi
    fi

elif [ "$OS" = "Linux" ]; then
    # Linux: requires dev packages -- on Debian/Ubuntu:
    #   sudo apt install build-essential cmake ninja-build pkg-config \
    #       libasound2-dev libjack-jackd2-dev \
    #       libfreetype6-dev libfontconfig1-dev \
    #       libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
    #       libxcomposite-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev
    CMAKE_BIN="${CMAKE_BIN:-cmake}"
    JOBS="${JOBS:-$(nproc)}"
    GEN="${GEN:-Ninja}"

    "$CMAKE_BIN" -S "$ROOT" -B "$BUILD_DIR" \
        -G "$GEN" \
        -DCMAKE_BUILD_TYPE="$CONFIG"

    "$CMAKE_BIN" --build "$BUILD_DIR" --config "$CONFIG" -j "$JOBS"

    EXE="$BUILD_DIR/NeuralStage_artefacts/NeuralStage"
    if [ -x "$EXE" ]; then
        echo ""
        echo "Built: $EXE"
        if [ "${LAUNCH:-0}" = "1" ]; then
            "$EXE" &
        fi
    fi

else
    echo "ERROR: unsupported OS: $OS (use build.ps1 on Windows)" >&2
    exit 1
fi
