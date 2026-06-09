# NeuralStage

**Live Guitar Rig for Plugin Players** — a NAM-first performance host for
guitarists and bassists, in the spirit of Gig Performer / MainStage / Patchworx.

## Build

### Windows (Visual Studio 2022 + ASIO)

```powershell
./build.ps1
```

Output: `Builds\NeuralStage_artefacts\Release\NeuralStage.exe`.
Includes ASIO, WASAPI (shared + exclusive) and DirectSound audio backends. The
ASIO SDK is shared from `AtiNAMatiC-Tone/juce-vst3-bridge/ASIOSDK`.

### macOS (Apple Silicon)

```bash
./build.sh
```

Uses `/opt/homebrew/bin/cmake` (native arm64). Output:
`Builds/NeuralStage_artefacts/Release/NeuralStage.app`.

### Linux (Debian / Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev \
    libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
    libxcomposite-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev
./build.sh
```

Output: `Builds/NeuralStage_artefacts/NeuralStage`. ALSA + JACK audio backends.

JUCE 8.0.4 is fetched via `FetchContent` on first configure.

## Stack

- **Framework:** JUCE 8 + CMake (C++20)
- **DSP core:** NeuralAmpModelerCore (vendored, `ThirdParty/NeuralAmpModelerCore`)
- **Plugin hosting:** VST3 (all platforms) + AU (macOS)
- **Audio I/O:** ASIO + WASAPI + DirectSound (Windows), CoreAudio (macOS), ALSA + JACK (Linux)
- **Bundle ID:** `com.neuralstage.app`

See `Docs/Architecture.md` and `Docs/UI-Spec.md`.
