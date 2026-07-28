# Quanpressor DSP architecture

## Common realtime path

Input, output, makeup, mix, Drive, Threshold, Ratio, Knee, Attack, and Release
use sample-rate-aware smoothing. Discrete mode and quality changes use
preallocated crossfade paths. Detector processing is separate from nonlinear
audio processing, and gain reduction is transferred to nonlinear sample rates
with bounded linear interpolation. A preallocated control history compensates
the upsampling FIR group delay before interpolation, so audio, gain reduction,
and Drive reach each nonlinear cell at the same source time. Attenuation is
clamped to the range `0.001..1.0`, so the gain envelope cannot create positive
gain.

The dry path and all native, 2x, and 4x wet paths use the same fixed latency.
Mode changes use a 30 ms crossfade, quality changes use a 10 ms crossfade, and
stereo-link changes use a 5 ms control-state transition. FET ratio selections
and Vari-mu time-constant selections use a 5 ms bounded control-voltage
crossfade. These paths allocate their buffers and delay storage during
`prepare`.

## VCA

VCA is a modern feed-forward design. Its RMS detector and gain computer operate
in the dB domain, with Threshold, continuous Ratio, and Knee applied directly
to the static curve. Auto Release blends independent fast, slow, and sustain
states. The gain cell remains nearly transformerless at Drive 0; its small
nonlinearity increases with control voltage and gain-reduction depth.

## OPTO

OPTO uses a sidechain level stage, soft full-wave rectification, light-source
drive, and separate fast and slow optical memories. Signal duration and
gain-reduction depth increase the slow-memory contribution. Ratio is a
continuous optical-loop-strength compatibility control. The LDR attenuation is
clean; the tube and output transformer stages provide the primary coloration.
Stereo link uses a shared optical state. Photocell reset clears both optical
memories and returns through a short control crossfade.

## FET

FET uses post-gain-cell feedback detection. Its four ratio selections have
separate feedback amount, knee width, transient overshoot, and coloration
calibration. Attack maps to approximately 20 through 800 microseconds, and
the FET popup and scale report that mapped detector time directly. The source
artwork is `Assets/FET_Attack_Scale.svg`; the editor dynamically selects
`Assets/FET_Attack_Scale.png` in FET mode and
`Assets/Normal_Attack_Scales.png` in every other mode. Release maps to
approximately 50 through 1100 milliseconds. The control voltage, feedback
gain, and FET nonlinearity share one state path and are bounded against
runaway gain.

## Vari-mu

Vari-mu uses transformer input coloration, post-output feedback detection, a
rectifier, six time-constant networks, and a push-pull variable-mu gain stage.
Ratio controls loop strength, while Knee controls the rectifier transition.
The Ratio labels are compatibility control values rather than fixed output
slopes, so the 20:1 setting remains progressive and does not match a VCA 20:1
static curve.
The global Attack and Release controls act as documented compatibility trims
around the selected hardware time constant. Gain-reduction depth couples tube
bias, transconductance, and controlled branch mismatch. The transfer law is
monotonic over the supported input range.

The six time-constant settings use the following nominal attack and release
networks before the global 0.75x through 1.25x compatibility trims:

| Setting | Attack | Individual peak | Multiple peaks | Sustained program |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0.2 ms | 0.3 s | - | - |
| 2 | 0.2 ms | 0.8 s | - | - |
| 3 | 0.4 ms | 2 s | - | - |
| 4 | 0.4 ms | 5 s | - | - |
| 5 | 0.4 ms | 2 s | 10 s | 10 s |
| 6 | 0.2 ms | 0.3 s | 10 s | 25 s |

Settings 5 and 6 use independent transient, multiple-peak, and sustained
program envelopes. The 10-second memory charges across repeated compression,
while the 25-second setting-6 memory requires consistently high program
material. Short isolated peaks therefore recover through the fast branch
without fully charging either long memory.

The audio gain cell uses a Quanpressor-local, C2-continuous push-pull transfer.
It remains exactly linear through low amplitudes, blends into saturation from
0.20 through 0.50 linear amplitude, and then follows the calibrated tube
saturation curve. This removes the discontinuity in the underlying piecewise
helper without modifying Patina or removing the intended low-order tube
coloration.

## Quality modes

Native, 2x, and 4x modes remain available, and the default parameter remains
native for session compatibility. The 2x and 4x paths use Patina linear-phase
half-band filters at High quality, with an approximately 96 dB stopband target.
The maximum 4x round-trip latency is 92 base-rate samples. Native and 2x paths,
the dry reference, and all modes are delayed to that fixed latency.

Each architecture's complete nonlinear wet path now runs at the selected
sample rate. This includes the FET amplifier and gain cell, OPTO LDR/tube path,
VCA cell and light saturation, Vari-mu transformer/tube path, and the
architecture-calibrated output transformer. Output-transformer blend is
normalised by the quality rate so its state update does not make 2x and 4x
unintentionally more coloured. Makeup, auto level, dry/wet mix, and output trim
remain at base rate after downsampling.

Quality automation uses a 10 ms linear crossfade. Detector envelopes, optical
memory, control smoothing, and control history continue across a quality
change. Patina's private audio-rate tube and magnetic states cannot be
converted between sample rates, so the destination audio path is reset before
the bounded crossfade. No allocation or state construction occurs on the audio
thread.
