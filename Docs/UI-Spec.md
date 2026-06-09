# NeuralStage UI Spec (pixel-perfect)

## Canvas
- 1400 × 800 px (desktop base)
- 8 px spacing grid; scale: 8 / 16 / 24 / 32 / 40 / 48 / 64

## Layout
- **Top bar** — 64 px high, 24 px side padding. `NEURALSTAGE` (28 px SemiBold) left, CPU/Latency (14 px) right.
- **Amp panel** — 920 × 520, centered, `radius: 20`, `padding: 32`.
  - Row 1 (6 knobs, 40 px gap): INPUT GAIN BASS MID TREBLE PRES → 776 px wide.
  - Row 2 (3 knobs, 48 px gap): TIGHT BODY AIR.
  - Sweet Spot meter: 480 × 24, radius 12, 32 px below row 2. Zones: green 60% / yellow 25% / red 15%. Outer purple glow blur 12 / 20% opacity.
  - Model label: 16 px, +1.5 letter spacing, 24 px below meter.
- **Signal chain bar** — 72 px high. Blocks 120 × 40, radius 10, 16 px gap. `[GATE]→[DRIVE]→[NAM AMP]→[IR CAB]→[FX]`.
- **Scene bar** — 80 px high. Buttons 120 × 56, radius 14, 16 px gap, 4 scenes.

## Knobs
- 96 × 96 px body, 4 px indicator width × 28 px length.
- Rotation range 300°, start −150°, end +150°.
- Drop shadow Y 4 / blur 12 / 25%. Hover scale 1.05, active scale 0.97.
- Active glow: accent purple, blur 16, 25%.

## Typography
- Inter / SF Pro / Roboto.
- Preset Title: 28 px SemiBold.
- Section Label: 14 px Medium ALL CAPS, +1.5 tracking.
- Knob Label: 12 px Medium, +1 tracking.
- Value: 16 px Bold.
- Small: 12 px.

## Colors
| Token            | Hex      |
|------------------|----------|
| Background       | #0E0E12  |
| Panel            | #181820  |
| Panel Light      | #20202A  |
| Accent           | #6C5CE7  |
| Accent Hover     | #7D6BFF  |
| Accent Glow      | #A29BFE  |
| Text Primary     | #EAEAF0  |
| Text Secondary   | #A0A0B0  |
| Text Disabled    | #5A5A6A  |
| Green            | #00E676  |
| Yellow           | #FFD54F  |
| Red              | #FF5252  |

## Scaling
- Design at 100%; support 75 / 100 / 125 / 150%.
- Vector knobs (or filmstrip ≥ 192 px base) for crispness.

## Performance / Gig Mode (later)
- Full-screen, simplified.
- Big GAIN/VOL knobs, huge scene buttons, expression bar.

## Cab View (later)
- Speaker image + draggable mic dot.
- Mic type & distance dropdowns.
- IR 1 / IR 2 + Blend slider.
