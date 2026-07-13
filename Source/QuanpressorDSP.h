#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <patina.h>
#include <array>
#include <cmath>
#include <atomic>

class QuanpressorDSP
{
public:
    enum Mode { FET = 0, OPTO, VCA, VARIMU };

    struct Controls
    {
        Mode mode = FET;
        float inputDb = 0.0f, thresholdDb = -18.0f, ratio = 4.0f, driveDb = 0.0f;
        float attackMs = 10.0f, releaseMs = 150.0f, kneeDb = 6.0f;
        float makeupDb = 0.0f, outputDb = 0.0f, mix = 1.0f;
        float highBleedHz = 8000.0f, highBleedAmt = 0.0f;
        float lowBleedHz = 120.0f, lowBleedAmt = 0.0f;
        int varMuSpeed = 1;
        bool autoLevel = false, autoRelease = false, grMatch = false;
        bool sidechainHpf = false, stereoChain = true;
    };


    // Monotonic program-dependent Vari-Mu display/target law.
    // The raw Patina 6386 exponential gain law can exceed a 1:infinity slope
    // in dB-domain static plots, which makes the displayed transfer curve fold
    // downward. Quanpressor keeps the Patina detector, time constants and tube
    // stages, but remaps the GR amount to a progressive, always-monotonic law.
    static float variableMuGainReductionDb(float overDb) noexcept
    {
        overDb = juce::jmax(0.0f, overDb);
        constexpr float kneeDb = 6.0f;
        const float effectiveOver = overDb < kneeDb
            ? overDb * overDb / (2.0f * kneeDb)
            : overDb - kneeDb * 0.5f;

        constexpr float initialSlope = 2.0f / 3.0f; // about 1.5:1 near onset
        constexpr float limitingSlope = 0.08f;       // approaches about 12.5:1
        constexpr float transitionDb = 4.5f;
        const float outputOver = limitingSlope * effectiveOver
            + (initialSlope - limitingSlope) * transitionDb
                * (1.0f - std::exp(-effectiveOver / transitionDb));
        return juce::jmax(0.0f, effectiveOver - outputOver);
    }

    static float variableMuOverDbForGainReduction(float grDb) noexcept
    {
        grDb = juce::jmax(0.0f, grDb);
        float lo = 0.0f, hi = 96.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (variableMuGainReductionDb(mid) < grDb) lo = mid;
            else hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    void prepare(double sr, int maxBlock, int channels)
    {
        sampleRate = sr;
        numChannels = channels;
        patina::ProcessSpec spec { sr, maxBlock, channels };
        for (auto& e : engines) e.prepare(spec);
        for (auto& e : gainEngines) e.prepare(spec);
        outputTransformers.resize((size_t) juce::jmax(1, channels));
        for (auto& transformer : outputTransformers)
            transformer.prepare(1, sr);
        lowSplit.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        highSplit.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        sidechainHigh.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        sidechainHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        sidechainHigh.setCutoffFrequency(120.0f);
        allocatedSamples = juce::jmax(maxBlock, 65536);
        for (auto* scratch : { &inputReference, &detectorBuffer, &modeInput, &oldWet,
                               &lowBand, &highLowPassed })
            scratch->setSize(juce::jmax(1, channels), allocatedSamples, false, true, false);
        reset();
    }

    void reset()
    {
        for (auto& e : engines) e.reset();
        for (auto& e : gainEngines) e.reset();
        for (auto& transformer : outputTransformers)
            transformer.reset();
        lowSplit.reset();
        highSplit.reset();
        sidechainHigh.reset();
        currentMode = requestedMode = oldMode = FET;
        xfadeRemaining = 0;
        smoothedGrDb = 0.0f;
        smoothedGrDbByChannel.fill(0.0f);
        autoGainDb = 0.0f;
        previousAutoEnabled = false;
        previousStereoChain = false;
        modeAverageGr.fill(0.0f);
        architectureTrimDb.fill(0.0f);
        previousDriveActive = false;
        displayGraphGainDb.store(0.0f, std::memory_order_relaxed);
    }

    void requestPhotoCellReset() noexcept
    {
        photoResetRequested.store(true, std::memory_order_release);
    }

    void process(juce::AudioBuffer<float>& buffer, const Controls& c)
    {
        const int n = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        if (n == 0 || channels == 0) return;

        if (photoResetRequested.exchange(false, std::memory_order_acq_rel))
        {
            for (auto& e : engines)
                e.photo.reset();
            for (auto& e : gainEngines)
                e.photo.reset();
            modeAverageGr[(size_t) OPTO] = 0.0f;
            if (currentMode == OPTO)
            {
                smoothedGrDb = 0.0f;
                smoothedGrDbByChannel.fill(0.0f);
            }
        }

        if (c.stereoChain && !previousStereoChain)
        {
            engines[(size_t) currentMode].reset();
            gainEngines[(size_t) currentMode].reset();
            if (xfadeRemaining > 0)
            {
                engines[(size_t) oldMode].reset();
                gainEngines[(size_t) oldMode].reset();
            }
        }
        previousStereoChain = c.stereoChain;

        requestedMode = c.mode;
        if (requestedMode != currentMode && xfadeRemaining == 0)
        {
            oldMode = currentMode;
            currentMode = requestedMode;

            // A mode that was visited earlier may still contain a detector or
            // gain-cell envelope. Re-enter it from a defined state; the old mode
            // remains untouched because it is still needed for the crossfade.
            engines[(size_t) currentMode].reset();
            gainEngines[(size_t) currentMode].reset();
            modeAverageGr[(size_t) currentMode] = 0.0f;
            xfadeRemaining = juce::roundToInt(0.15 * sampleRate);
        }

        lowSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        highSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        lowSplit.setCutoffFrequency(juce::jlimit(30.0f, 600.0f, c.lowBleedHz));
        highSplit.setCutoffFrequency(juce::jlimit(1000.0f, 18000.0f, c.highBleedHz));

        const float inputGain = juce::Decibels::decibelsToGain(c.inputDb);
        const float appliedMakeupDb = c.autoLevel ? 0.0f : c.makeupDb;
        const float makeupGain = juce::Decibels::decibelsToGain(appliedMakeupDb);
        const float outputGain = juce::Decibels::decibelsToGain(c.outputDb);

        jassert(n <= allocatedSamples && channels <= inputReference.getNumChannels());
        if (n > allocatedSamples || channels > inputReference.getNumChannels())
            return;
        for (auto* scratch : { &inputReference, &detectorBuffer, &modeInput, &oldWet,
                               &lowBand, &highLowPassed })
            scratch->setSize(channels, n, false, false, true);

        copyBuffer(inputReference, buffer, channels, n);
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::multiply(inputReference.getWritePointer(ch), inputGain, n);

        copyBuffer(detectorBuffer, inputReference, channels, n);
        if (c.sidechainHpf)
        {
            juce::dsp::AudioBlock<float> scBlock(detectorBuffer);
            scBlock = scBlock.getSubBlock(0, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> scContext(scBlock);
            sidechainHigh.process(scContext);
        }

        copyBuffer(buffer, inputReference, channels, n);
        copyBuffer(modeInput, inputReference, channels, n);
        const float effectiveThreshold = juce::jlimit(-48.0f, 0.0f, c.thresholdDb);
        displayThresholdDb.store(effectiveThreshold, std::memory_order_relaxed);
        std::array<float, 2> newGrByChannel {};
        const float newGr = processMode(buffer, detectorBuffer, currentMode, c, effectiveThreshold,
                                        &newGrByChannel);

        double oldSq = 0.0, newSq = bufferEnergy(buffer, channels, n);
        if (xfadeRemaining > 0)
        {
            copyBuffer(oldWet, modeInput, channels, n);
            const float oldThreshold = juce::jlimit(-48.0f, 0.0f, c.thresholdDb);
            processMode(oldWet, detectorBuffer, oldMode, c, oldThreshold);
            oldSq = bufferEnergy(oldWet, channels, n);

            const float oldDb = rmsDb(oldSq, n * channels);
            const float newDb = rmsDb(newSq, n * channels);
            const float difference = juce::jlimit(-9.0f, 9.0f, oldDb - newDb);
            architectureTrimDb[(size_t) currentMode] += 0.08f
                * (difference - architectureTrimDb[(size_t) currentMode]);

            const float newAlign = juce::Decibels::decibelsToGain(architectureTrimDb[(size_t) currentMode]);
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch), newAlign, n);

            const int totalFade = juce::jmax(1, juce::roundToInt(0.15 * sampleRate));
            for (int i = 0; i < n; ++i)
            {
                if (xfadeRemaining > 0)
                {
                    const float t = 1.0f - (float) xfadeRemaining / (float) totalFade;
                    const float oldMix = std::sqrt(juce::jlimit(0.0f, 1.0f, 1.0f - t));
                    const float newMix = std::sqrt(juce::jlimit(0.0f, 1.0f, t));
                    for (int ch = 0; ch < channels; ++ch)
                        buffer.setSample(ch, i, oldWet.getSample(ch, i) * oldMix
                                              + buffer.getSample(ch, i) * newMix);
                    --xfadeRemaining;
                }
            }
        }
        else
        {
            const float align = juce::Decibels::decibelsToGain(architectureTrimDb[(size_t) currentMode]);
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch), align, n);
        }

        // Parallel frequency-selective dry bleed, based on the input after INPUT GAIN.
        copyBuffer(lowBand, inputReference, channels, n);
        copyBuffer(highLowPassed, inputReference, channels, n);
        juce::dsp::AudioBlock<float> lowBlock(lowBand), highBlock(highLowPassed);
        lowBlock = lowBlock.getSubBlock(0, (size_t) n);
        highBlock = highBlock.getSubBlock(0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock), highContext(highBlock);
        lowSplit.process(lowContext);
        highSplit.process(highContext);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* lo = lowBand.getReadPointer(ch);
            const auto* hiLowPassed = highLowPassed.getReadPointer(ch);
            const auto* dry = inputReference.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const float hi = dry[i] - hiLowPassed[i];
                wet[i] += lo[i] * c.lowBleedAmt + hi * c.highBleedAmt;
            }
        }

        const double inputSq = bufferEnergy(inputReference, channels, n);
        const double wetSq = bufferEnergy(buffer, channels, n);
        const float inputDbNow = rmsDb(inputSq, n * channels);
        const float wetDbNow = rmsDb(wetSq, n * channels);
        displayGraphGainDb.store(juce::jlimit(-48.0f, 24.0f, wetDbNow - inputDbNow),
                                 std::memory_order_relaxed);

        if (c.autoLevel)
        {
            const float desired = juce::jlimit(-12.0f, 12.0f, inputDbNow - wetDbNow);
            const float timeMs = desired > autoGainDb ? 250.0f : 900.0f;
            const float alpha = 1.0f - std::exp(-(float) n / (float) (sampleRate * timeMs * 0.001));
            autoGainDb += alpha * (desired - autoGainDb);
        }
        else
        {
            const float alpha = 1.0f - std::exp(-(float) n / (float) (sampleRate * 0.25));
            autoGainDb += alpha * (0.0f - autoGainDb);
        }
        previousAutoEnabled = c.autoLevel;

        // Apply wet-only gain compensation first, then perform true parallel
        // compression against the signal after INPUT GAIN. OUTPUT GAIN remains
        // post-mix so it affects dry and wet equally.
        const float wetGain = makeupGain * juce::Decibels::decibelsToGain(autoGainDb);
        const float mix = juce::jlimit(0.0f, 1.0f, c.mix);
        const float drivePosition = juce::jlimit(0.0f, 1.0f, c.driveDb / 18.0f);
        const bool driveActive = drivePosition > 0.0001f;
        if (!driveActive && previousDriveActive)
        {
            // Clear only the audio-path colour state. Detector engines retain
            // their envelopes, so returning Drive to zero cannot leave magnetic
            // or gain-cell memory while compression continues smoothly.
            gainEngines[(size_t) currentMode].reset();
            for (auto& transformer : outputTransformers)
                transformer.reset();
        }
        previousDriveActive = driveActive;

        TransformerModel::Params transformerParams;
        // Keep the transformer far below obvious saturation. TransformerModel has
        // a fairly strong magnetic transfer even at low parameter values, so the
        // processed signal is blended in rather than replacing the clean output.
        transformerParams.driveDb = -9.0f + c.driveDb * 0.72f;
        transformerParams.saturation = 0.01f + drivePosition * 0.16f;
        transformerParams.enableLfBoost = false;
        transformerParams.enableHfRolloff = false;
        transformerParams.dcBias = 0.0f;
        transformerParams.temperature = 40.0f;

        const float transformerLinear = 1.0f + transformerParams.saturation * 2.0f;
        const float transformerTrim = 1.0f / (transformerLinear * transformerLinear);
        const float architectureTransformerAmount = currentMode == FET ? 0.22f
                                                : currentMode == OPTO ? 0.025f
                                                : currentMode == VCA ? 0.0f : 0.18f;
        const float transformerMix = drivePosition * architectureTransformerAmount;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = inputReference.getReadPointer(ch);
            auto& transformer = outputTransformers[(size_t) juce::jmin(ch, (int) outputTransformers.size() - 1)];
            for (int i = 0; i < n; ++i)
            {
                const float mixed = (wet[i] * wetGain * mix + dry[i] * (1.0f - mix)) * outputGain;
                if (transformerMix > 0.0f)
                {
                    const float coloured = transformer.process(0, mixed, transformerParams) * transformerTrim;
                    wet[i] = mixed + (coloured - mixed) * transformerMix;
                }
                else
                {
                    wet[i] = mixed;
                }
            }
        }

        const float blockSeconds = (float) n / (float) sampleRate;
        const float averageAlpha = 1.0f - std::exp(-blockSeconds / 0.5f);
        modeAverageGr[(size_t) currentMode] += averageAlpha
            * (newGr - modeAverageGr[(size_t) currentMode]);
        const float meterAlpha = 1.0f - std::exp(-blockSeconds / 0.08f);
        smoothedGrDb += meterAlpha * (newGr - smoothedGrDb);
        for (size_t ch = 0; ch < smoothedGrDbByChannel.size(); ++ch)
        {
            const float target = static_cast<int>(ch) < channels ? newGrByChannel[ch]
                                                                 : newGrByChannel[0];
            smoothedGrDbByChannel[ch] += meterAlpha * (target - smoothedGrDbByChannel[ch]);
        }

    }

    float getGrDb() const noexcept { return smoothedGrDb; }
    float getGrDbForChannel(int channel) const noexcept
    {
        return smoothedGrDbByChannel[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    }
    float getEffectiveThresholdDb() const noexcept { return displayThresholdDb.load(std::memory_order_relaxed); }
    float getGraphGainDb() const noexcept { return displayGraphGainDb.load(std::memory_order_relaxed); }

private:
    struct EngineSet
    {
        PhotoCompressor photo;
        FetCompressor fet;
        VariableMuCompressor mu;
        VcaCompressor vca;

        void prepare(const patina::ProcessSpec& s)
        {
            photo.prepare(s); fet.prepare(s); mu.prepare(s); vca.prepare(s);
        }
        void reset()
        {
            photo.reset(); fet.reset(); mu.reset(); vca.reset();
        }
    };

    static double bufferEnergy(const juce::AudioBuffer<float>& b, int channels, int samples)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* p = b.getReadPointer(ch);
            for (int i = 0; i < samples; ++i)
                sum += (double) p[i] * (double) p[i];
        }
        return sum;
    }

    static float rmsDb(double energy, int count)
    {
        const double mean = energy / (double) juce::jmax(1, count);
        return juce::Decibels::gainToDecibels((float) std::sqrt(juce::jmax(mean, 1.0e-20)), -100.0f);
    }

    static float thresholdDriveGain(float desiredThresholdDb, float modelThresholdDb)
    {
        return juce::Decibels::decibelsToGain(modelThresholdDb - desiredThresholdDb);
    }

    static void copyBuffer(juce::AudioBuffer<float>& destination,
                           const juce::AudioBuffer<float>& source,
                           int channels, int samples) noexcept
    {
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::copy(destination.getWritePointer(ch),
                                              source.getReadPointer(ch), samples);
    }

    float processMode(juce::AudioBuffer<float>& b,
                      const juce::AudioBuffer<float>& detector,
                      Mode mode, const Controls& c,
                      float effectiveThresholdDb,
                      std::array<float, 2>* channelGrDb = nullptr)
    {
        auto& e = engines[(size_t) mode];
        double grSum = 0.0;
        int grCount = 0;
        std::array<double, 2> channelGrSum {};
        std::array<int, 2> channelGrCount {};

        const float attackPosition = juce::jlimit(0.0f, 1.0f,
            (c.attackMs - 0.02f) / (100.0f - 0.02f));
        const float attackNorm = std::sqrt(attackPosition);
        float effectiveReleaseMs = c.releaseMs;
        if (c.autoRelease && (mode == FET || mode == VCA))
        {
            const float grDepth = juce::jlimit(0.0f, 18.0f,
                std::abs(modeAverageGr[(size_t) mode]));
            const float depth = grDepth / 18.0f;
            const float fastMs = mode == FET ? 65.0f : 110.0f;
            const float slowMs = mode == FET ? 520.0f : 900.0f;
            effectiveReleaseMs = juce::jmap(depth * depth, fastMs, slowMs);
        }

        const float releasePosition = juce::jlimit(0.0f, 1.0f,
            (effectiveReleaseMs - 20.0f) / (1200.0f - 20.0f));
        const float releaseNorm = std::sqrt(releasePosition);

        // Drive pushes each architecture's native Patina gain cell. The VCA is
        // intentionally the cleanest; FET is most assertive, while OPTO and
        // Vari-Mu retain their distinct single-ended and push-pull tube shapes.
        const float architectureDriveScale = mode == FET ? 1.35f
                                           : mode == OPTO ? 0.82f
                                           : mode == VCA ? 0.28f : 0.68f;
        const float driveGain = juce::Decibels::decibelsToGain(
            juce::jlimit(0.0f, 18.0f, c.driveDb) * architectureDriveScale);
        const float inverseDrive = 1.0f / juce::jmax(1.0f, driveGain);

        FetCompressor::Params fetParams;
        fetParams.inputGain = std::sqrt(juce::jlimit(0.0f, 1.0f,
            (juce::Decibels::decibelsToGain(juce::jmax(0.0f, -24.0f - effectiveThresholdDb)) - 1.0f) / 39.0f));
        fetParams.outputGain = 0.5f;
        fetParams.attack = attackNorm;
        fetParams.release = releaseNorm;
        fetParams.ratio = c.ratio < 6.0f ? 0 : c.ratio < 10.0f ? 1 : c.ratio < 16.0f ? 2 : 3;

        PhotoCompressor::Params photoParams;
        photoParams.peakReduction = juce::jlimit(0.0f, 1.0f, 0.24f + c.ratio / 28.0f);
        photoParams.outputGain = 0.5f;
        photoParams.mode = c.ratio >= 8.0f ? 1 : 0;
        photoParams.mix = 1.0f;

        VcaCompressor::Params vcaParams;
        vcaParams.threshold = juce::jlimit(0.0f, 1.0f, (effectiveThresholdDb + 40.0f) / 40.0f);
        vcaParams.ratio = juce::jlimit(0.0f, 1.0f,
            std::sqrt(juce::jmax(0.0f, (c.ratio - 1.5f) / 48.5f)));
        vcaParams.attack = attackNorm;
        vcaParams.release = releaseNorm;
        vcaParams.outputGain = 0.5f;
        vcaParams.kneeMode = c.kneeDb <= 0.0f ? 1 : 0;
        vcaParams.kneeDb = juce::jlimit(0.0f, 18.0f, c.kneeDb);

        VariableMuCompressor::Params muParams;
        muParams.inputGain = 0.0f;
        muParams.threshold = juce::jlimit(0.0f, 1.0f, (effectiveThresholdDb + 40.0f) / 40.0f);
        muParams.outputGain = 0.5f;
        muParams.timeConstant = juce::jlimit(0, 5, c.varMuSpeed);

        const float thresholdLinear = juce::Decibels::decibelsToGain(effectiveThresholdDb);
        const float fetDetectorScale = effectiveThresholdDb > -24.0f
            ? thresholdDriveGain(effectiveThresholdDb, -24.0f) : 1.0f;
        const float belowPatinaRangeScale = effectiveThresholdDb < -40.0f
            ? thresholdDriveGain(effectiveThresholdDb, -40.0f) : 1.0f;

        auto& gainEngine = gainEngines[(size_t) mode];
        const auto detectGainReduction = [&](int detectorChannel, float detectorSample)
        {
            float grDb = 0.0f;
            switch (mode)
            {
                case FET:
                {
                    // Patina's FET sidechain is RMS-based while the graph and
                    // INPUT meter are peak-referenced. Calibrate a sine's crest
                    // factor so the displayed threshold and actual onset agree.
                    constexpr float peakToRmsReference = 1.41421356237f;
                    (void)e.fet.process(detectorChannel,
                        detectorSample * fetDetectorScale * peakToRmsReference, fetParams);
                    grDb = e.fet.getGainReductionDb(detectorChannel);
                    break;
                }
                case OPTO:
                {
                    const float aboveThreshold = juce::jmax(0.0f, std::abs(detectorSample) - thresholdLinear);
                    (void)e.photo.process(detectorChannel, aboveThreshold, photoParams);
                    grDb = e.photo.getGainReductionDb(detectorChannel);
                    break;
                }
                case VCA:
                    (void)e.vca.processWithSidechain(detectorChannel, 0.0f,
                        detectorSample * belowPatinaRangeScale, vcaParams);
                    grDb = e.vca.getGainReductionDb(detectorChannel);
                    break;
                case VARIMU:
                {
                    (void)e.mu.processWithSidechain(detectorChannel, 0.0f,
                        detectorSample * belowPatinaRangeScale, muParams);
                    const float rawGrDb = e.mu.getGainReductionDb(detectorChannel);
                    constexpr float rawDbPerEffectiveOver =
                        (20.0f / 2.302585093f) * 3.0f / 20.0f;
                    const float effectiveOver = juce::jmax(0.0f, -rawGrDb / rawDbPerEffectiveOver);
                    constexpr float kneeDb = 6.0f;
                    const float overDb = effectiveOver < kneeDb * 0.5f
                        ? std::sqrt(juce::jmax(0.0f, effectiveOver * 2.0f * kneeDb))
                        : effectiveOver + kneeDb * 0.5f;
                    grDb = -variableMuGainReductionDb(overDb);
                    break;
                }
            }
            return juce::jmin(0.0f, grDb);
        };

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float linkedGrDb = 0.0f;
            if (c.stereoChain && detector.getNumChannels() > 1)
            {
                float commonDetector = 0.0f;
                for (int ch = 0; ch < detector.getNumChannels(); ++ch)
                    commonDetector = juce::jmax(commonDetector, std::abs(detector.getSample(ch, i)));
                linkedGrDb = detectGainReduction(0, commonDetector);
            }

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                auto* p = b.getWritePointer(ch);
                const float grDb = c.stereoChain && detector.getNumChannels() > 1
                    ? linkedGrDb
                    : detectGainReduction(ch, detector.getSample(ch, i));
                const float linearGain = juce::Decibels::decibelsToGain(grDb);
                const float drivenSample = p[i] * driveGain;

                switch (mode)
                {
                    case FET:
                        p[i] = gainEngine.fet.processWithGain(ch, drivenSample, linearGain, fetParams) * inverseDrive;
                        break;
                    case OPTO:
                        p[i] = gainEngine.photo.processWithGain(ch, drivenSample, linearGain, photoParams) * inverseDrive;
                        break;
                    case VCA:
                        p[i] = gainEngine.vca.processWithGain(ch, drivenSample, linearGain, vcaParams) * inverseDrive;
                        break;
                    case VARIMU:
                        p[i] = gainEngine.mu.processWithGain(ch, drivenSample, linearGain, muParams) * inverseDrive;
                        break;
                }
                grSum += juce::jmin(0.0f, grDb);
                ++grCount;
                if (ch < static_cast<int>(channelGrSum.size()))
                {
                    channelGrSum[static_cast<size_t>(ch)] += juce::jmin(0.0f, grDb);
                    ++channelGrCount[static_cast<size_t>(ch)];
                }
            }
        }

        if (channelGrDb != nullptr)
        {
            for (size_t ch = 0; ch < channelGrDb->size(); ++ch)
            {
                if (channelGrCount[ch] > 0)
                    (*channelGrDb)[ch] = static_cast<float>(channelGrSum[ch]
                                                          / static_cast<double>(channelGrCount[ch]));
                else
                    (*channelGrDb)[ch] = ch > 0 ? (*channelGrDb)[0] : 0.0f;
            }
        }

        return grCount > 0 ? (float) (grSum / (double) grCount) : 0.0f;
    }

    std::array<EngineSet, 4> engines;
    std::array<EngineSet, 4> gainEngines;
    std::vector<TransformerModel> outputTransformers;
    juce::AudioBuffer<float> inputReference, detectorBuffer, modeInput, oldWet;
    juce::AudioBuffer<float> lowBand, highLowPassed;
    juce::dsp::StateVariableTPTFilter<float> lowSplit, highSplit, sidechainHigh;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int allocatedSamples = 0;
    Mode currentMode = FET, requestedMode = FET, oldMode = FET;
    int xfadeRemaining = 0;
    float smoothedGrDb = 0.0f;
    std::array<float, 2> smoothedGrDbByChannel {};
    float autoGainDb = 0.0f;
    bool previousAutoEnabled = false;
    bool previousStereoChain = false;
    bool previousDriveActive = false;
    std::array<float, 4> modeAverageGr {};
    std::array<float, 4> architectureTrimDb {};
    std::atomic<bool> photoResetRequested { false };
    std::atomic<float> displayThresholdDb { -18.0f };
    std::atomic<float> displayGraphGainDb { 0.0f };
};
