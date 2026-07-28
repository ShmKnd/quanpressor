# Quanpressor DSP measurements

The measurement harness records the baseline at commit `6396dcb`, the
architecture revision immediately before Phase 6, and the final Phase 6
working tree with the same test implementation and configuration matrix.
Each CSV file contains 8,424 valid data rows.

Files:

- `baseline-6396dcb.csv`: original DSP behavior.
- `pre-phase6.csv`: architecture revision with Medium-quality OS filters.
- `after-working-tree.csv`: final architecture and Phase 6 OS behavior.
- `static-curves.svg`: 48 kHz, native, stereo-linked static curves.
- `thd-drive.svg`: 48 kHz, native, stereo-linked THD Drive sweeps.
- `timing-alias.svg`: 48 kHz timing and 10 kHz alias summary.
- `phase6-alias.svg`: pre-Phase 6 and Phase 6 OS alias comparison.

The harness covers:

- static input/output and gain-reduction curves from -60 to 0 dBFS;
- a high-ratio gain-reduction depth sweep that exceeds 20 dB;
- attack and release step timing;
- 1 kHz THD and harmonics 2 through 12;
- 10 kHz folded alias energy;
- 19/20 kHz two-tone and five-tone intermodulation;
- Drive from 0 to 18 dB in 3 dB steps;
- 44.1, 48, and 96 kHz;
- native, High-quality 2x, and High-quality 4x processing;
- mono, stereo-linked, and stereo-unlinked operation;
- automation at block sizes 1, 16, 64, 512, and 8192.

Every output sample is checked for a finite value, denormal value, and an
unexpectedly large peak. Impulse measurements verify zero-sample-error dry-path
alignment for every mode and quality. Each CSV row includes the revision label,
commit, mode, sample rate, block size, topology, quality, measurement name,
unit, and validity result.

Representative revised results at 48 kHz, native, stereo-linked:

| Mode | THD at Drive 0 | Attack t63 | Release t63 | 10 kHz alias |
| --- | ---: | ---: | ---: | ---: |
| FET | 0.5148% | 0.0208 ms | 115.69 ms | -17.47 dBc |
| OPTO | 0.0764% | 16.67 ms | 195.33 ms | -32.35 dBc |
| VCA | 0.0177% | 2.67 ms | 787.00 ms | -77.70 dBc |
| VARIMU | 0.0258% | 0.33 ms | 300.33 ms | -25.10 dBc |

At Drive 0, VCA is the cleanest mode. Phase 6 changed the 48 kHz, Drive 18,
4x alias results as follows:

| Mode | Pre-Phase 6 | Phase 6 | Reduction |
| --- | ---: | ---: | ---: |
| FET | -61.78 dBc | -66.53 dBc | 4.75 dB |
| OPTO | -67.70 dBc | -70.51 dBc | 2.81 dB |
| VCA | -78.32 dBc | -86.95 dBc | 8.63 dB |
| VARIMU | -52.70 dBc | -56.80 dBc | 4.10 dB |

High-quality 4x processing sets the shared fixed latency to 92 base-rate
samples. Every native, 2x, 4x, and dry-path impulse measurement reported zero
sample error.

The standalone performance test covers mono and stereo at 44.1, 48, and
96 kHz, using 64-sample blocks, 4x quality, and Drive 18 dB. In the worst-case
stereo 96 kHz configuration, the measured realtime factors on the development
Mac were 12.26x for FET, 13.51x for OPTO, 13.68x for VCA, and 9.44x for
VARIMU. All 24 mode/rate/topology configurations remained above 9x realtime.
These figures include test-signal generation and are machine-specific. The
benchmark source is `Tests/RealtimePerformanceTest.cpp`.

At 48 kHz native quality, the VARIMU second harmonic moved from -74.57 dBc at
Drive 0 to -57.84 dBc at Drive 18. This records the controlled branch mismatch;
the balanced low-Drive case has lower even-order content.

Regenerate the plots with:

```sh
python3 Tests/plot_measurements.py \
  Measurements/baseline-6396dcb.csv \
  Measurements/after-working-tree.csv \
  Measurements \
  Measurements/pre-phase6.csv
```

The test signal, projection window, settling interval, and pass conditions are
defined in `Tests/DspMeasurementHarness.cpp`. The fixed-latency and per-mode
4x alias regression limits are also enforced by
`Tests/OversamplingQualityTest.cpp`. The local Vari-mu gain-cell curve is
checked for continuity and monotonicity by
`Tests/VariMuPushPullContinuityTest.cpp`.
