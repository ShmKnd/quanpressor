# Quanpressor DSP architecture improvement plan

## 1. Purpose

FET, OPTO, VCA, and Vari-mu modes currently use dedicated Patina models, but
their detectors and audio gain cells are driven through a mostly common
feed-forward structure. This plan improves the behavior and distortion of each
mode so that it more closely follows its representative hardware architecture.

The implementation must preserve realtime safety, existing session recall, mono
and stereo operation, dry/wet phase alignment, and the current mode-transition
behavior.

## 2. Scope and constraints

- Primary implementation target:
  - `Source/QuanpressorDSP.h`
  - `Source/PluginProcessor.cpp`
  - `Source/PluginProcessor.h`
- Modify GUI files only when a mode-specific control or indication is required.
- Preserve the existing project-generation system. Do not migrate the project
  to CMake.
- Do not modify Patina source, headers, configuration, build settings, or
  dependency version without explicit prior approval.
- If a required behavior cannot be implemented through the current Patina API,
  stop and present:
  - the missing capability;
  - the proposed Patina change;
  - an implementation alternative that leaves Patina unchanged.
- Do not implement or enable a new oversampling design until it has been
  separately reviewed and explicitly approved.
- Do not change parameter IDs. Existing sessions and automation must remain
  loadable.

## 3. Completion criteria

The work is complete when:

- each mode has an architecture-specific detector and timing behavior;
- gain reduction and distortion respond coherently to signal level and control
  state;
- Drive automation does not create clicks or zipper noise;
- no audio-thread allocations or locks are introduced;
- mono, dual-mono, and stereo-linked operation pass automated tests;
- mode changes and oversampling changes do not produce discontinuities;
- measured aliasing, THD, static curves, and timing curves are documented before
  and after the changes;
- all existing parameter states remain compatible.

## 4. Phase 0 - Baseline and measurement harness

Status: pending

### Tasks

1. Record the current output of every mode before changing DSP.
2. Add repeatable offline measurements for:
   - static input/output curve from -60 dBFS to 0 dBFS;
   - gain reduction versus level;
   - attack and release step response;
   - 1 kHz THD and harmonic distribution;
   - 10 kHz aliasing;
   - two-tone and multitone intermodulation;
   - Drive sweep from 0 dB to 18 dB;
   - gain-reduction sweep from 0 dB to at least 20 dB;
   - 44.1, 48, and 96 kHz sample rates;
   - native, 2x, and 4x nonlinear processing;
   - mono, stereo linked, and stereo unlinked operation.
3. Save numerical results in a machine-readable format and render comparison
   plots outside the plugin binary.
4. Check parameter automation at block sizes 1, 16, 64, 512, and the maximum
   advertised block size.

### Acceptance criteria

- Repeated runs produce equivalent results within floating-point tolerance.
- Baseline files clearly identify commit, sample rate, block size, mode, and
  parameter values.
- The harness detects NaN, infinity, denormals, unexpectedly large output, and
  latency mismatch.

## 5. Phase 1 - Common realtime and control-path corrections

Status: pending

### Tasks

1. Add sample-rate-independent smoothing for:
   - Input Gain;
   - Output Gain;
   - Makeup Gain;
   - Mix;
   - Drive;
   - Threshold;
   - continuously variable Ratio and Knee controls.
2. Keep discrete controls discrete. Crossfade changes that cannot be safely
   interpolated, including mode, FET ratio selection, Vari-mu time constant, and
   oversampling factor.
3. Separate audio-rate nonlinear processing from detector-rate processing.
4. Replace generic interpolation of the gain-reduction signal with a monotonic
   envelope interpolation method that cannot overshoot above unity or ring
   around fast GR changes.
5. Define a mode-specific stereo-link signal:
   - FET: peak or rectifier control voltage;
   - OPTO: smoothed light-drive state;
   - VCA: detector level in the log/dB domain;
   - Vari-mu: rectified control voltage.
6. Verify that resetting Drive coloration state cannot alter the active
   detector envelope.

### Acceptance criteria

- Fast automation produces no discontinuity greater than the agreed test
  threshold.
- Gain envelopes remain finite, monotonic during simple steps, and never create
  positive gain when only attenuation is requested.
- No memory allocation, mutex, file access, or unbounded work occurs on the
  audio thread.

## 6. Phase 2 - VCA mode

Status: pending

VCA is the closest current mode to its intended architecture and should be used
as the first architecture-specific implementation.

### Design decision

Select one reference architecture before implementation:

- modern feed-forward VCA compressor; or
- dbx-style hard-knee VCA; or
- SSL-style bus compressor.

Do not combine their detector and timing traits without documenting the hybrid.

### Tasks

1. Implement an explicit log/dB-domain detector and gain computer.
2. Apply Threshold, Ratio, and Knee in that gain computer.
3. Map Attack and Release to VCA-specific time constants.
4. Replace the current average-GR Auto Release calculation with a fast and slow
   release-state blend.
5. Make VCA distortion depend on control voltage and GR depth, not only on the
   signal boost applied by Drive.
6. Keep the standard VCA path effectively transformerless. If transformer color
   remains available, treat it as an optional output color rather than a core
   part of the VCA topology.

### Acceptance criteria

- Static curves match the requested Threshold, Ratio, and Knee.
- Auto Release returns quickly after short peaks and more slowly after sustained
  compression.
- At Drive 0 dB, VCA is the cleanest of the four modes.
- Increasing GR produces a controlled and documented change in distortion.

## 7. Phase 3 - OPTO mode

Status: pending

### Tasks

1. Remove the instantaneous `abs(sample) - thresholdLinear` excitation law.
2. Model the control path as:
   - sidechain amplifier;
   - full-wave rectifier;
   - light-source drive;
   - optical memory;
   - LDR attenuation.
3. Maintain at least two release states:
   - a fast release component;
   - a slow memory component whose contribution increases with GR depth and
     signal duration.
4. Map the current Ratio parameter to a continuous compatibility control without
   discontinuity. Internally treat it as optical-loop strength or interpolate
   between Compress and Limit behavior.
5. Keep the LDR attenuation comparatively clean.
6. Move the primary OPTO coloration to separate tube-amplifier and transformer
   stages.
7. Base stereo linking on the shared optical control state rather than the
   instantaneous maximum audio sample.
8. Preserve the existing photocell reset command and reset all new optical
   memory states deterministically.

### Acceptance criteria

- No hard detector discontinuity occurs at Threshold.
- Release becomes measurably slower after a long or deeply compressed signal.
- Short isolated peaks recover faster than sustained material.
- OPTO distortion is dominated by tube/output coloration rather than an
  artificially overdriven LDR.
- Photocell reset produces a defined state without a click.

## 8. Phase 4 - FET mode

Status: pending

### Tasks

1. Change the FET control topology from input-derived feed-forward detection to
   a feedback loop derived from the post-gain-cell signal.
2. Keep the detector, FET control voltage, and nonlinear FET transfer in one
   coherent state path.
3. Map Attack to an approximately 20-800 us FET range.
4. Map Release to an approximately 50-1100 ms range.
5. Preserve the 4:1, 8:1, 12:1, and 20:1 ratio selections.
6. Give each ratio a calibrated feedback amount, knee, transient overshoot, and
   distortion response.
7. Model input/output amplifier and transformer coloration separately from the
   FET attenuator.
8. Do not add an All-buttons mode unless it is implemented as a distinct
   feedback, bias, and timing behavior.

### Acceptance criteria

- Increasing input level changes both GR and FET coloration in a correlated way.
- The static and dynamic curves remain stable under the feedback loop.
- Attack reaches the expected sub-millisecond range.
- Ratio modes are measurably distinct in static curve and transient response.
- The implementation cannot enter runaway gain, oscillation, or non-finite
  state.

## 9. Phase 5 - Vari-mu mode

Status: pending

### Tasks

1. Build a feedback topology based on:
   - transformer-coupled input;
   - push-pull variable-mu gain stage;
   - output sensing;
   - rectifier;
   - time-constant network;
   - control-voltage feedback to the tube bias.
2. Keep the transfer curve monotonic by correcting the bias/control-voltage law,
   rather than fully replacing the tube model's GR with an unrelated external
   curve.
3. Couple GR, tube bias, transconductance, and distortion.
4. Represent the push-pull branches separately enough to support:
   - cancellation of even harmonics in the balanced case;
   - residual even harmonics from controlled mismatch;
   - increased nonlinear behavior at deep GR.
5. Preserve and calibrate the six time-constant selections.
6. Define the compatibility behavior of the global Ratio, Attack, Release, and
   Knee parameters. Parameters that do not apply must not silently imply a
   physical control.

### Acceptance criteria

- Output remains monotonic across the full supported input range.
- Ratio increases progressively with level rather than changing abruptly.
- Deep GR changes harmonic balance in a repeatable way.
- The balanced push-pull case has lower even-order content than the deliberately
  mismatched case.
- All six time constants have documented attack and multi-stage release curves.

## 10. Phase 6 - Nonlinear oversampling

Status: design only - explicit approval required before implementation

### Proposed design

- Quality modes:
  - 2x for normal operation;
  - 4x for high quality;
  - native mode retained for compatibility and CPU comparison.
- Oversample every audio-path nonlinear stage that can generate audible
  harmonics:
  - FET gain cell and nonlinear amplifiers;
  - OPTO tube stages and transformers;
  - VCA nonlinear cell when enabled;
  - Vari-mu tube stages and transformers;
  - common output transformer, if retained.
- Keep slow detector and timing networks at base rate unless a nonlinear
  rectifier requires a higher internal rate.
- Upsample the GR/control signal with bounded monotonic interpolation instead of
  treating it as an ordinary audio channel.
- Preallocate all buffers and filter state in `prepareToPlay`.
- Keep latency constant across modes and quality changes, or report and update
  it safely outside active audio processing.
- Crossfade between old and new processing paths when oversampling is automated.
- Preserve detector, optical-memory, magnetic-memory, and tube-bias states when
  changing quality whenever a valid state conversion is possible.

### Evaluation before approval

Measure:

- alias energy above and below 15 kHz;
- THD and harmonic balance;
- transient smearing;
- reported latency and dry/wet null;
- CPU usage for mono and stereo at 44.1, 48, and 96 kHz;
- behavior while automating quality mode;
- state reset and session reload behavior.

Approval should select the default state, 2x/4x filter quality, latency policy,
and acceptable CPU budget.

## 11. Phase 7 - Integration, compatibility, and release validation

Status: pending

### Tasks

1. Run the full baseline suite and generate before/after comparisons.
2. Test old saved states with every mode and oversampling setting.
3. Verify parameter IDs, ranges, defaults, and normalization.
4. Verify dry/wet alignment with impulse and null tests.
5. Verify mode crossfades at silence, steady tones, transients, and sustained GR.
6. Verify stereo image stability when enabling and disabling stereo linking.
7. Profile worst-case CPU and confirm no realtime allocation.
8. Test Windows-compatible project paths and settings.
9. Inspect all staged files before any commit and exclude build products,
   binaries, caches, and generated artifacts.

## 12. Recommended implementation order

1. Baseline measurement harness.
2. Common smoothing and control-envelope handling.
3. VCA architecture.
4. OPTO architecture and memory.
5. FET feedback architecture.
6. Vari-mu feedback and push-pull behavior.
7. Oversampling review and approval.
8. Approved oversampling implementation.
9. Full regression, compatibility, and performance validation.

Each mode phase should be measured, reviewed, and accepted before beginning the
next mode. This keeps regressions attributable and avoids changing all four
sonic signatures at once.
