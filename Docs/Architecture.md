# NeuralStage Architecture

## Positioning

> Live Guitar Rig for Plugin Players.

A NAM-first performance host for guitarists/bassists, in the spirit of
Gig Performer / MainStage / Patchworx.

## High-level Modules

```
Source/
├── App.{h,cpp}                  JUCEApplication entry / DocumentWindow
├── UI/                          MainComponent, panels, bars, components, styles
├── Audio/                       AudioEngine + DSP nodes
│   ├── Processors/              Input, Output (gain stages, metering)
│   ├── NAM/                     NAMLoader, NAMProcessor, NAMModel
│   ├── IR/                      IRLoader (juce::dsp::Convolution)
│   └── FX/                      NoiseGate, EQ (more later)
├── PluginHost/                  3rd-party VST3/AU hosting
├── MIDI/                        Input + learn mappings
├── Core/                        AppState, PresetManager, SceneManager
└── Utils/                       Logger, paths
```

## Signal Flow

Default chain:

```
Input → Pre Gain → [Noise Gate] → [Drive] → NAM Amp → IR Cab → [FX] → Post Gain → Output
```

Dual-NAM topology (later):

```
Split Input → NAM A
            → NAM B
            → Blend Mixer → IR → Output
```

NAM stage internally:

```
buffer → preGain → NAM inference → postGain → buffer
```

## Data Models

`NAMModel`:
- `filePath`
- `sampleRate`
- `inputGain`, `outputGain`
- `tags` (clean, high gain, …)
- `displayName`

`Scene`: snapshot of all parameters (4 per preset for MVP).
`Preset`: name + signal-chain config + 4 scenes + plugin slots.

## Threading

- **Audio thread**: `AudioEngine::audioDeviceIOCallbackWithContext` — only DSP, no allocs.
- **Message thread**: UI, file I/O, plugin scanning, preset save/load.
- Atomics for parameter passing into DSP. ValueTree on message thread, mirrored to atomics.

## Build

- CMake 3.22+, native arm64 cmake on macOS (`/opt/homebrew/bin/cmake`).
- JUCE 8.0.4 fetched via `FetchContent` (or vendor at `ThirdParty/JUCE`).
- NAM core: TBD — likely sdatkinson/NeuralAmpModelerCore as submodule under `ThirdParty/NAM`.

## Roadmap (post-MVP)

1. Wire NeuralAmpModelerCore into `NAMProcessor`.
2. Cab panel with draggable mic position + dual IR blend.
3. VST3/AU plugin slots in FX section.
4. Scenes capture/recall full state.
5. Performance Mode (full-screen big knobs / huge scene buttons).
6. MIDI learn for footswitch mapping.
7. Tuner.
