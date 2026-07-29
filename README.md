# Quanpressor

Four-character compressor for JUCE 8, built on the supplied Patina DSP library.

Modes: FET / OPTO / VCA / VARI-MU.

Implemented:
- unified Threshold workflow for every mode (FET threshold maps to Patina's internal sensitivity control)
- Input, Threshold, Ratio, Attack, Release, Knee, Makeup, Output
- host-visible mono or stereo external sidechain input with an explicit EXT SC switch
- factory presets plus load, save, and delete support for user presets
- high/low spectral bleed controls
- Auto Level Match
- mode-switch loudness compensation
- GR Match (slow automatic threshold trim toward the pre-switch average GR)
- 150 ms dual-engine crossfade when switching modes
- input/output meters, GR meter, transfer curve, and scrolling GR history
- Vari-Mu FAST / MID / SLOW time-constant buttons

On macOS, Quanpressor stores user presets, license data, and trial state under
`~/Library/Audio/Presets/SoundRave/Quanpressor`. It does not create or update
Quanpressor data under `~/Library/Application Support`.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

CMake fetches JUCE 8.0.6. Patina is vendored under `Patina/`.

## Licensing

Quanpressor verifies signed SoundRave license files with libsodium. An
unlicensed installation runs as a 14-day trial; after the trial, periodic
low-level noise is added until a valid `*.soundravelicense` file is loaded.

libsodium is distributed under the ISC License. Copyright (c) 2013-2026 Frank
Denis. Permission to use, copy, modify, and/or distribute the software for any
purpose with or without fee is granted provided that the copyright and
permission notices appear in all copies. The software is provided "AS IS"
without warranty, and the author is not liable for damages arising from its
use or performance.
