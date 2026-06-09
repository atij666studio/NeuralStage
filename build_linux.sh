#!/usr/bin/env bash
# =============================================================================
#  build_linux.sh — NeuralStage standalone, Linux x86_64 or ARM64 (native)
#
#  Works on Debian/Ubuntu/Raspberry Pi OS (Bookworm or newer).
#  Run from the repo root on the target machine.
#
#  Install prerequisites once:
#    sudo apt update
#    sudo apt install -y \
#      cmake ninja-build build-essential pkg-config git \
#      libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev \
#      libgl1-mesa-dev libgles2-mesa-dev libx11-dev libxrandr-dev \
#      libxinerama-dev libxcursor-dev libxcomposite-dev libxext-dev \
#      libpipewire-0.3-dev libspa-0.2-dev \
#      libgtk-3-dev libwebkit2gtk-4.1-dev
#
#  Raspberry Pi 5 note:
#    Same command above — Pi OS Bookworm ships all these packages.
#    Pi OS uses PipeWire by default; ALSA and JACK are also available.
#    Build natively on the Pi itself (no cross-compilation needed; Pi 5 is
#    fast enough to build JUCE in ~15–20 minutes).
#
#  macOS / Windows: use build.ps1 (Windows) or cmake -G Xcode (macOS).
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/Builds/Linux"
CORES=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

echo ""
echo "==> NeuralStage Linux build"
echo "    Source : $SCRIPT_DIR"
echo "    Output : $BUILD_DIR"
echo "    Cores  : $CORES"
echo ""

# Detect architecture for display only
ARCH=$(uname -m)
echo "    Arch   : $ARCH"
echo ""

# Prefer Ninja if available; fall back to Unix Makefiles
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi
echo "==> Generator: $GENERATOR"

echo ""
echo "==> Configuring..."
cmake \
    -S "$SCRIPT_DIR" \
    -B "$BUILD_DIR" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "==> Building Release..."
cmake --build "$BUILD_DIR" --config Release --parallel "$CORES"

echo ""
echo "==> Build complete. Artifacts:"

EXE=$(find "$BUILD_DIR" -name "NeuralStage.v.*" -not -name "*.so" -not -name "*.vst3" 2>/dev/null | head -1)
if [[ -n "$EXE" ]]; then
    echo "    Standalone : $EXE  ($(du -h "$EXE" | cut -f1))"
fi

VST3=$(find "$BUILD_DIR" -name "NeuralStage.vst3" -type d 2>/dev/null | head -1)
if [[ -n "$VST3" ]]; then
    echo "    VST3       : $VST3"
fi

echo ""
echo "==> To install VST3 system-wide:"
echo "    sudo cp -r $VST3 /usr/lib/vst3/"
echo ""
echo "==> To run the standalone:"
echo "    $EXE"
echo ""
