# Changelog

All notable changes to **NeuralStage** are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [0.2.1] — 2026-06-09

### Added

- **LV2 plugin hosting.** The signal-chain slots now accept LV2 plugins on
  Windows, macOS and Linux, scanned from the standard LV2 paths
  (`~/.lv2/`, `/usr/lib/lv2/`, etc.).
- **Audio Unit (AU) hosting** on macOS — AU plugins appear in the slot pickers
  alongside VST3 and LV2.
- **NeuralStage as a CLAP plugin.** `NeuralStage.clap` is now installed to the
  system CLAP folder so any CLAP-compatible DAW can load NeuralStage as an
  instrument/effect plugin.
- **Linux standalone.** `build_linux.sh` added to the repo root. Supports
  x86_64 and ARM64 (Raspberry Pi 5 native compile). Audio backends: ALSA,
  JACK and PipeWire.
- **GitHub Actions CI** (`.github/workflows/build-linux.yml`) — automatically
  builds Linux x86_64 and ARM64 binaries on every push; artifacts downloadable
  from the Actions tab without needing a local Linux machine.
- **`build_wsl.ps1`** — builds the Linux binary from Windows via WSL
  (installs WSL automatically if not present).

### Changed

- SCAN now indexes VST3 and LV2 plugin folders in one pass.
- §2, §3 and §6 of the manual updated to reflect multi-format hosting.

---

## [0.2.0] — 2026-06-05

### Added

- **Per-preset scene banks.** Every preset now embeds all four scenes (each
  with its own NAM models, XY puck position, per-slot on/off, signal chains
  and knobs) plus the scene that was active when the preset was saved.
  Loading a preset swaps the entire 4-scene bank and recalls the saved active
  scene.
- **XY puck position and per-slot on/off state** are now captured into scenes
  and presets.
- **Selectable NAM output modes** — Raw / Normalized / Calibrated — gain-matched
  to a fixed −18 dBu reference.
- **Warm pool** that pre-loads the plugins referenced by every scene at boot
  for faster, lower-latency scene switching, plus an immediate splash on launch.
- MIDI input now picks up controllers **hot-plugged while the app is running**.

### Changed

- **NAM tone fidelity.** Models now run at the native sample rate they were
  trained at via an internal high-quality resampler (matching the reference NAM
  plugin). The previous fixed global oversampling that subtly altered clarity
  and low end was removed — captures sound identical at any session sample rate.
- **XY morph pad** now uses a true equal-power crossfade normalised across only
  the loaded slots, so perceived loudness is constant across the pad with no
  centre dip, and empty slots never steal energy.
- Puck moves, slot loads/clears, and slot bypasses are smoothed over a ~20 ms
  ramp on the audio thread (zipper-/click-free).

### Fixed

- **Click-free scene switching.** Scene recall briefly ducks the master output
  (fast fade out / hold / fade in) across the swap, so any chain or NAM-model
  discontinuity is inaudible — no pops, clicks, or silence gaps between scenes.
- **Switching presets now changes the sound of every scene**, not just the
  SCENE button labels (presets previously shared one global scene bank).
- **MIDI Learn / footswitches no longer stop responding** after opening the
  Audio / MIDI Settings dialog — MIDI input listeners are re-asserted when the
  dialog closes.

### Compatibility

- Presets saved before v0.2.0 still load (as a single state) for backward
  compatibility. Re-save them to attach the new four-scene bank.

---

## [0.1.0]

### Added

- Initial release: 4-NAM XY morph host, signal-chain strip, scenes, presets,
  looper, backing track, tuner, auto-leveller, noise gate, offline render,
  project bundles, crash recovery, and a fully themed UI.

[0.2.1]: #021--2026-06-09
[0.2.0]: #020--2026-06-05
[0.1.0]: #010
