# NeuralStage v0.1.0 — Screenshot Capture List

Capture at native window size, PNG, no JPEG. Drop into
`Docs/Screenshots/` using the filenames below so the manual references
resolve.

## Group A — Main window zones

| # | Filename | What to capture |
|---|---|---|
| 01 | `01-main-window-full.png` | Full window, default rig loaded, nothing muted. |
| 02 | `02-main-window-mute-on.png` | Same as 01 but **MUTE** badge red on INPUT knob. |
| 03 | `03-amp-knobs-strip.png` | Top row of 9 amp knobs only (INPUT..AIR). |
| 04 | `04-amp-knob-mute-overlay.png` | Close-up of INPUT knob with the MUTE overlay button visible. |
| 05 | `05-signal-chain-strip-empty.png` | Signal chain strip with no VST3s loaded (only NAM/IR highlighted teal). |
| 06 | `06-signal-chain-strip-full.png` | Same strip with GATE/COMP/DRIVE/EQ/MOD/DELAY/REVERB/LIMIT/MASTER FX all populated. |
| 07 | `07-left-rail-input.png` | Left INPUT rail: Sweet Spot + Auto Lvl + readouts + Tuner. |
| 08 | `08-tuner-active.png` | Close-up of tuner showing a detected note + cents offset; mute speaker visible. |
| 09 | `09-xy-morph-pad-centred.png` | NAM XY pad with puck centred; all four slots (A/B/C/D) populated. |
| 10 | `10-xy-morph-pad-dragged.png` | Same pad with puck dragged off-centre toward A. |
| 11 | `11-pitch-doubler-panel.png` | Right rail close-up: TRANSPOSE / WIDTH / DOUBLER MIX / OUTPUT. |
| 12 | `12-sweet-spot-meter-perfect.png` | Sweet-spot strip lit on **PERFECT**. |
| 13 | `13-stats-bar.png` | Red stats bar close-up: CPU / smp@Hz / latency / GLITCH. |
| 14 | `14-scene-bar.png` | Scene bar with SCENE 2 active; NEURAL and STAGE wordmarks visible. |
| 15 | `15-bottom-toolbar.png` | Full bottom toolbar row close-up. |
| 16 | `16-spectrum-overlay-on.png` | Main window with **SPEC** toggled — spectrum analyser overlaid. |

## Group B — Pop-ups / dialogs (themed)

| # | Filename | What to capture |
|---|---|---|
| 17 | `17-about-dialog.png` | About dialog opened by clicking NEURAL or STAGE wordmark. |
| 18 | `18-audio-midi-settings.png` | Themed Audio / MIDI Settings dialog. |
| 19 | `19-presets-browser.png` | Preset browser with at least 3 presets listed. |
| 20 | `20-midi-assignments-table.png` | MIDI Assignments table with at least 2 bindings shown. |
| 21 | `21-footswitch-wizard.png` | Footswitch wizard panel showing the 12 targets. |
| 22 | `22-offline-render-dialog.png` | Offline Render Stems dialog with all stem checkboxes visible. |
| 23 | `23-noise-gate-dialog.png` | Noise Gate floating window, GATE OFF. |
| 24 | `24-looper-window.png` | Looper window with REC/PLAY/STOP/CLEAR/COUNT-IN/METRONOME row. |
| 25 | `25-backing-track-window.png` | Backing Track window with a file loaded, progress bar visible. |

## Group C — Hosted plugin window (themed chrome)

| # | Filename | What to capture |
|---|---|---|
| 26 | `26-hosted-plugin-editor.png` | A loaded VST3 (any) showing the themed dark title bar with NeuralStage chrome around the plugin's own UI. |
| 27 | `27-hosted-plugin-on-top.png` | Same plugin editor sitting above the main window after a SCENE click (proves always-on-top). |

## Group D — System messages

| # | Filename | What to capture |
|---|---|---|
| 28 | `28-session-restored-popup.png` | Themed "Session restored" popup after a forced crash. |
| 29 | `29-scan-needs-auth-list.png` | "Plugins needing authentication" list after a SCAN right-click. |

29 shots total. Capture at 100% display scaling, no DPI scaling artefacts.
Save as 24-bit PNG (no alpha) to keep the file sizes manageable.
