# Quanpressor

Four-character compressor for JUCE 8, built on the supplied Patina DSP library.

Modes: FET / OPTO / VCA / VARI-MU.

Implemented:
- unified Threshold workflow for every mode (FET threshold maps to Patina's internal sensitivity control)
- Input, Threshold, Ratio, Attack, Release, Knee, Makeup, Output
- high/low spectral bleed controls
- Auto Level Match
- mode-switch loudness compensation
- GR Match (slow automatic threshold trim toward the pre-switch average GR)
- 150 ms dual-engine crossfade when switching modes
- input/output meters, GR meter, transfer curve, and scrolling GR history
- Vari-Mu FAST / MID / SLOW time-constant buttons

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

CMake fetches JUCE 8.0.6. Patina is vendored under `Patina/`.
