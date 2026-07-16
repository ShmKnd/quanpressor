#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <patina.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <vector>

class QuanpressorDSP
{
public:
    enum Mode { FET = 0, OPTO, VCA, VARIMU };
    static constexpr double modeCrossfadeSeconds = 0.030;
    static constexpr double stereoLinkCrossfadeSeconds = 0.005;

    struct Controls
    {
        Mode mode = FET;
        float inputDb = 0.0f, thresholdDb = -18.0f, ratio = 4.0f, driveDb = 0.0f;
        float attackMs = 10.0f, releaseMs = 150.0f, kneeDb = 6.0f;
        float makeupDb = 0.0f, outputDb = 0.0f, mix = 1.0f;
        float highBleedHz = 8000.0f, highBleedAmt = 0.0f;
        float lowBleedHz = 120.0f, lowBleedAmt = 0.0f;
        int varMuSpeed = 1, oversamplingFactor = 2;
        bool autoLevel = false, autoRelease = false, grMatch = false;
        bool sidechainHpf = false, stereoChain = true, oversampling = true;
    };

    static int fetRatioIndex(float parameterValue) noexcept
    {
        static constexpr std::array<float, 4> ratios { 4.0f, 8.0f, 12.0f, 20.0f };
        int nearestIndex = 0;
        float nearestDistance = std::abs(parameterValue - ratios[0]);
        for (int i = 1; i < static_cast<int>(ratios.size()); ++i)
        {
            const float distance = std::abs(parameterValue - ratios[static_cast<size_t>(i)]);
            if (distance < nearestDistance)
            {
                nearestIndex = i;
                nearestDistance = distance;
            }
        }
        return nearestIndex;
    }

    static float fetRatioValue(float parameterValue) noexcept
    {
        static constexpr std::array<float, 4> ratios { 4.0f, 8.0f, 12.0f, 20.0f };
        return ratios[static_cast<size_t>(fetRatioIndex(parameterValue))];
    }


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
        nonlinearMaxBlock = juce::jmax(1, maxBlock);
        const int nonlinearChannels = juce::jmax(1, channels) * 2;
        nonlinearLatencySamples = 0;
        for (size_t mode = 0; mode < nonlinearBuffers.size(); ++mode)
        {
            nonlinearBuffers[mode].setSize(nonlinearChannels, nonlinearMaxBlock);
            nonlinearOversamplers2x[mode].prepare(nonlinearMaxBlock, nonlinearChannels,
                patina::OversamplingFactor::x2, patina::FilterQuality::Medium);
            nonlinearOversamplers4x[mode].prepare(nonlinearMaxBlock, nonlinearChannels,
                patina::OversamplingFactor::x4, patina::FilterQuality::Medium);
            resetNonlinearOversampler(nonlinearOversamplers2x[mode]);
            resetNonlinearOversampler(nonlinearOversamplers4x[mode]);
            nonlinearLatencySamples = juce::jmax(nonlinearLatencySamples,
                                                  nonlinearOversamplers4x[mode].getLatency());
        }
        inputPathDelay.prepare(channels, nonlinearLatencySamples);
        for (auto& delay : nativeModeDelays)
            delay.prepare(channels, nonlinearLatencySamples);
        for (size_t mode = 0; mode < nonlinearBuffers.size(); ++mode)
        {
            oversampling2xAlignmentDelays[mode].prepare(
                channels, nonlinearLatencySamples - nonlinearOversamplers2x[mode].getLatency());
            oversampling4xAlignmentDelays[mode].prepare(
                channels, nonlinearLatencySamples - nonlinearOversamplers4x[mode].getLatency());
        }
        outputTransformers.resize((size_t) juce::jmax(1, channels));
        for (auto& transformer : outputTransformers)
            transformer.prepare(1, sr);
        lowSplit.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        highSplit.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        lowSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        highSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        lowSplitCutoffHz = 120.0f;
        highSplitCutoffHz = 8000.0f;
        lowSplit.setCutoffFrequency(lowSplitCutoffHz);
        highSplit.setCutoffFrequency(highSplitCutoffHz);
        sidechainHigh.prepare({ sr, (juce::uint32) maxBlock, (juce::uint32) channels });
        sidechainHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        sidechainHigh.setCutoffFrequency(120.0f);
        allocatedSamples = juce::jmax(maxBlock, 65536);
        for (auto* scratch : { &inputReference, &detectorBuffer, &modeInput, &oldWet,
                               &lowBand, &highLowPassed, &gainScratch })
            scratch->setSize(juce::jmax(1, channels), allocatedSamples, false, true, false);
        reset();
    }

    void reset()
    {
        for (auto& e : engines) e.reset();
        for (auto& e : gainEngines) e.reset();
        for (auto& oversampler : nonlinearOversamplers2x) resetNonlinearOversampler(oversampler);
        for (auto& oversampler : nonlinearOversamplers4x) resetNonlinearOversampler(oversampler);
        for (auto& delay : oversampling2xAlignmentDelays) delay.reset();
        for (auto& delay : oversampling4xAlignmentDelays) delay.reset();
        for (auto& delay : nativeModeDelays) delay.reset();
        inputPathDelay.reset();
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
        previousStereoChain = false;
        stereoChainStateInitialised = false;
        stereoLinkFadeRemaining = 0;
        stereoLinkFadeStartGrDb.fill(0.0f);
        for (auto& modeGr : lastDetectorGrDbByMode)
            modeGr.fill(0.0f);
        previousOversampling = true;
        previousOversamplingFactor = 2;
        modeAverageGr.fill(0.0f);
        transitionTrimDb = 0.0f;
        previousDriveActive = false;
        displayGraphGainDb.store(0.0f, std::memory_order_relaxed);
    }

    void requestPhotoCellReset() noexcept
    {
        photoResetRequested.store(true, std::memory_order_release);
    }

    void process(juce::AudioBuffer<float>& buffer, const Controls& c)
    {
        const int totalSamples = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        if (totalSamples == 0 || channels == 0)
            return;

        jassert(channels <= inputReference.getNumChannels());
        if (channels > inputReference.getNumChannels())
            return;

        // Some offline renderers exceed prepareToPlay's advertised maximum
        // block size. Process those blocks as zero-copy views so no block can
        // silently bypass compression and no allocation occurs on this thread.
        const int processBlockSize = juce::jmin(allocatedSamples, nonlinearMaxBlock);
        jassert(processBlockSize > 0);
        for (int offset = 0; offset < totalSamples; offset += processBlockSize)
        {
            const int chunkSamples = juce::jmin(processBlockSize, totalSamples - offset);
            juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(), channels,
                                           offset, chunkSamples);
            processChunk(chunk, c);
        }
    }

private:
    void processChunk(juce::AudioBuffer<float>& buffer, const Controls& c)
    {
        const int n = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();

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

        const int oversamplingFactor = c.oversamplingFactor >= 4 ? 4 : 2;
        if (c.oversampling != previousOversampling
            || oversamplingFactor != previousOversamplingFactor)
        {
            for (auto& oversampler : nonlinearOversamplers2x) resetNonlinearOversampler(oversampler);
            for (auto& oversampler : nonlinearOversamplers4x) resetNonlinearOversampler(oversampler);
            for (auto& delay : oversampling2xAlignmentDelays) delay.reset();
            for (auto& delay : oversampling4xAlignmentDelays) delay.reset();
            for (auto& delay : nativeModeDelays) delay.reset();
            previousOversampling = c.oversampling;
            previousOversamplingFactor = oversamplingFactor;
        }

        if (!stereoChainStateInitialised)
        {
            previousStereoChain = c.stereoChain;
            stereoChainStateInitialised = true;
        }
        else if (c.stereoChain != previousStereoChain)
        {
            stereoLinkFadeStartGrDb = lastDetectorGrDbByMode[(size_t) currentMode];
            stereoLinkFadeRemaining = stereoLinkCrossfadeSamples();
            previousStereoChain = c.stereoChain;
        }

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
            resetNonlinearOversampler(nonlinearOversamplers2x[(size_t) currentMode]);
            resetNonlinearOversampler(nonlinearOversamplers4x[(size_t) currentMode]);
            oversampling2xAlignmentDelays[(size_t) currentMode].reset();
            oversampling4xAlignmentDelays[(size_t) currentMode].reset();
            nativeModeDelays[(size_t) currentMode].reset();
            modeAverageGr[(size_t) currentMode] = 0.0f;
            transitionTrimDb = 0.0f;
            stereoLinkFadeRemaining = 0;
            xfadeRemaining = modeCrossfadeSamples();
        }

        const float requestedLowSplitHz = juce::jlimit(30.0f, 600.0f, c.lowBleedHz);
        if (requestedLowSplitHz != lowSplitCutoffHz)
        {
            lowSplitCutoffHz = requestedLowSplitHz;
            lowSplit.setCutoffFrequency(lowSplitCutoffHz);
        }

        const float requestedHighSplitHz = juce::jlimit(1000.0f, 18000.0f, c.highBleedHz);
        if (requestedHighSplitHz != highSplitCutoffHz)
        {
            highSplitCutoffHz = requestedHighSplitHz;
            highSplit.setCutoffFrequency(highSplitCutoffHz);
        }

        const float inputGain = juce::Decibels::decibelsToGain(c.inputDb);
        const float appliedMakeupDb = c.autoLevel ? 0.0f : c.makeupDb;
        const float makeupGain = juce::Decibels::decibelsToGain(appliedMakeupDb);
        const float outputGain = juce::Decibels::decibelsToGain(c.outputDb);

        jassert(n <= allocatedSamples);
        for (auto* scratch : { &inputReference, &detectorBuffer, &modeInput, &oldWet,
                               &lowBand, &highLowPassed, &gainScratch })
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

        if (xfadeRemaining > 0)
        {
            const int oldModeSamples = juce::jmin(n, xfadeRemaining);
            copyBuffer(oldWet, modeInput, channels, oldModeSamples);
            juce::AudioBuffer<float> oldWetBlock(oldWet.getArrayOfWritePointers(),
                                                  channels, oldModeSamples);
            juce::AudioBuffer<float> oldDetectorBlock(detectorBuffer.getArrayOfWritePointers(),
                                                       channels, oldModeSamples);
            const float oldThreshold = juce::jlimit(-48.0f, 0.0f, c.thresholdDb);
            processMode(oldWetBlock, oldDetectorBlock, oldMode, c, oldThreshold, nullptr, false);
            const double oldSq = bufferEnergy(oldWetBlock, channels, oldModeSamples);
            const double newSq = bufferEnergy(buffer, channels, oldModeSamples);

            const float oldDb = rmsDb(oldSq, oldModeSamples * channels);
            const float newDb = rmsDb(newSq, oldModeSamples * channels);
            if (oldDb > -80.0f && newDb > -80.0f)
            {
                const float difference = juce::jlimit(-9.0f, 9.0f, oldDb - newDb);
                constexpr double trimTrackingSeconds = 0.128;
                const float trimAlpha = 1.0f - std::exp(
                    -(float) oldModeSamples / (float) (sampleRate * trimTrackingSeconds));
                transitionTrimDb += trimAlpha * (difference - transitionTrimDb);
            }

            const int totalFade = modeCrossfadeSamples();
            for (int i = 0; i < oldModeSamples; ++i)
            {
                const float t = 1.0f - (float) xfadeRemaining / (float) totalFade;
                const float oldMix = std::sqrt(juce::jlimit(0.0f, 1.0f, 1.0f - t));
                const float newMix = std::sqrt(juce::jlimit(0.0f, 1.0f, t));
                const float temporaryAlign = juce::Decibels::decibelsToGain(
                    transitionTrimDb * (1.0f - t));
                for (int ch = 0; ch < channels; ++ch)
                    buffer.setSample(ch, i, oldWet.getSample(ch, i) * oldMix
                                          + buffer.getSample(ch, i) * temporaryAlign * newMix);
                --xfadeRemaining;
            }

            if (xfadeRemaining == 0)
                transitionTrimDb = 0.0f;
        }

        // All nonlinear modes have the same oversampling latency. Delay the dry
        // reference as well so Mix and the parallel bleed paths remain aligned.
        inputPathDelay.process(inputReference);

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
        const float architectureTransformerAmount = currentMode == FET ? 0.045f
                                                : currentMode == OPTO ? 0.020f
                                                : currentMode == VCA ? 0.010f : 0.045f;
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

public:
    float getGrDb() const noexcept { return smoothedGrDb; }
    float getGrDbForChannel(int channel) const noexcept
    {
        return smoothedGrDbByChannel[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    }
    float getEffectiveThresholdDb() const noexcept { return displayThresholdDb.load(std::memory_order_relaxed); }
    float getGraphGainDb() const noexcept { return displayGraphGainDb.load(std::memory_order_relaxed); }
    int getLatencySamples() const noexcept { return nonlinearLatencySamples; }
    int getModeCrossfadeRemainingSamples() const noexcept { return xfadeRemaining; }
    float getModeTransitionTrimDb() const noexcept { return transitionTrimDb; }
    int getStereoLinkCrossfadeRemainingSamples() const noexcept { return stereoLinkFadeRemaining; }

private:
    int modeCrossfadeSamples() const noexcept
    {
        return juce::jmax(1, juce::roundToInt(modeCrossfadeSeconds * sampleRate));
    }

    int stereoLinkCrossfadeSamples() const noexcept
    {
        return juce::jmax(1, juce::roundToInt(stereoLinkCrossfadeSeconds * sampleRate));
    }

    void resetNonlinearOversampler(patina::Oversampler<float>& oversampler) noexcept
    {
        oversampler.reset();
        const int audioChannels = juce::jmax(1, numChannels);
        for (int ch = audioChannels; ch < audioChannels * 2; ++ch)
            oversampler.resetChannel(ch, 1.0f);
    }

    class FixedDelay
    {
    public:
        void prepare(int channels, int samples)
        {
            delaySamples = juce::jmax(0, samples);
            storage.setSize(juce::jmax(1, channels), juce::jmax(1, delaySamples),
                            false, true, false);
            writePositions.assign((size_t)juce::jmax(1, channels), 0);
            reset();
        }

        void reset() noexcept
        {
            storage.clear();
            std::fill(writePositions.begin(), writePositions.end(), 0);
        }

        void process(juce::AudioBuffer<float>& buffer) noexcept
        {
            if (delaySamples <= 0)
                return;

            const int channels = juce::jmin(buffer.getNumChannels(), storage.getNumChannels());
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* samples = buffer.getWritePointer(ch);
                auto* delayed = storage.getWritePointer(ch);
                int position = writePositions[(size_t)ch];
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const float input = samples[i];
                    samples[i] = delayed[position];
                    delayed[position] = input;
                    if (++position >= delaySamples)
                        position = 0;
                }
                writePositions[(size_t)ch] = position;
            }
        }

    private:
        juce::AudioBuffer<float> storage;
        std::vector<int> writePositions;
        int delaySamples = 0;
    };

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
                      std::array<float, 2>* channelGrDb = nullptr,
                      bool advanceStereoLinkTransition = true)
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

        // FET Drive is handled inside Patina's JFET gain cell. The other modes
        // retain their architecture-specific input push into the native cell.
        const float architectureDriveScale = mode == OPTO ? 0.82f
                                           : mode == VCA ? 0.28f : 0.68f;
        const float driveGain = mode == FET ? 1.0f
            : juce::Decibels::decibelsToGain(
                juce::jlimit(0.0f, 18.0f, c.driveDb) * architectureDriveScale);
        const float inverseDrive = 1.0f / juce::jmax(1.0f, driveGain);

        FetCompressor::Params fetParams;
        fetParams.inputGain = std::sqrt(juce::jlimit(0.0f, 1.0f,
            (juce::Decibels::decibelsToGain(juce::jmax(0.0f, -24.0f - effectiveThresholdDb)) - 1.0f) / 39.0f));
        fetParams.outputGain = 0.5f;
        fetParams.attack = attackNorm;
        fetParams.release = releaseNorm;
        fetParams.ratio = fetRatioIndex(c.ratio);
        fetParams.driveDb = juce::jlimit(0.0f, 18.0f, c.driveDb);

        PhotoCompressor::Params photoParams;
        photoParams.peakReduction = juce::jlimit(0.0f, 1.0f, 0.24f + c.ratio / 28.0f);
        photoParams.outputGain = 0.5f;
        // Keep Patina's photocell detector in its continuous Compress topology.
        // Quanpressor maps the exposed Ratio control to attenuation strength
        // below, avoiding the former 8:1 Compress/Limit discontinuity.
        photoParams.mode = 0;
        photoParams.mix = 1.0f;
        const float optoRatioPosition = juce::jlimit(0.0f, 1.0f,
            (c.ratio - 1.5f) / (20.0f - 1.5f));
        const float optoAttenuationStrength = juce::jmap(optoRatioPosition, 5.0f, 20.0f);

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
                    const float photocellResponse = juce::jlimit(0.0f, 1.0f,
                        e.photo.getDetectorState(detectorChannel));
                    const float gain = 1.0f
                        / (1.0f + photocellResponse * optoAttenuationStrength);
                    grDb = juce::Decibels::gainToDecibels(gain, -60.0f);
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
            const bool linked = c.stereoChain && detector.getNumChannels() > 1;
            float linkedGrDb = 0.0f;
            if (linked)
            {
                float commonDetector = 0.0f;
                for (int ch = 0; ch < detector.getNumChannels(); ++ch)
                    commonDetector = juce::jmax(commonDetector, std::abs(detector.getSample(ch, i)));
                linkedGrDb = detectGainReduction(0, commonDetector);

                // Keep the unused detector channels following the same common
                // sidechain. Their output is discarded while linked, but their
                // analogue memory is ready if the link is disabled later.
                for (int ch = 1; ch < detector.getNumChannels(); ++ch)
                    (void) detectGainReduction(ch, commonDetector);
            }

            float linkTransition = 1.0f;
            if (advanceStereoLinkTransition && stereoLinkFadeRemaining > 0)
            {
                const int totalFade = stereoLinkCrossfadeSamples();
                linkTransition = 1.0f - (float) (stereoLinkFadeRemaining - 1)
                                              / (float) totalFade;
            }

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float targetGrDb = linked
                    ? linkedGrDb
                    : detectGainReduction(ch, detector.getSample(ch, i));
                const float grDb = advanceStereoLinkTransition && stereoLinkFadeRemaining > 0
                    ? stereoLinkFadeStartGrDb[(size_t) juce::jmin(ch, 1)]
                        + linkTransition * (targetGrDb
                            - stereoLinkFadeStartGrDb[(size_t) juce::jmin(ch, 1)])
                    : targetGrDb;
                lastDetectorGrDbByMode[(size_t) mode][(size_t) juce::jmin(ch, 1)] = grDb;
                const float linearGain = juce::Decibels::decibelsToGain(grDb);
                gainScratch.setSample(ch, i, linearGain);
                grSum += juce::jmin(0.0f, grDb);
                ++grCount;
                if (ch < static_cast<int>(channelGrSum.size()))
                {
                    channelGrSum[static_cast<size_t>(ch)] += juce::jmin(0.0f, grDb);
                    ++channelGrCount[static_cast<size_t>(ch)];
                }
            }

            if (advanceStereoLinkTransition && stereoLinkFadeRemaining > 0)
                --stereoLinkFadeRemaining;
        }

        auto& gainEngine = gainEngines[(size_t) mode];
        const auto processGainCell = [&](int channel, float sample, float linearGain,
                                         double processingRateScale) noexcept
        {
            const float drivenSample = sample * driveGain;
            switch (mode)
            {
                case FET:
                    return gainEngine.fet.processWithGain(channel, sample, linearGain,
                                                          fetParams, processingRateScale);
                case OPTO:
                    return gainEngine.photo.processWithGain(channel, drivenSample, linearGain,
                                                            photoParams) * inverseDrive;
                case VCA:
                    return gainEngine.vca.processWithGain(channel, drivenSample, linearGain,
                                                          vcaParams) * inverseDrive;
                case VARIMU:
                    return gainEngine.mu.processWithGain(channel, drivenSample, linearGain,
                                                         muParams) * inverseDrive;
            }
            return sample;
        };

        if (c.oversampling)
        {
            const bool use4x = c.oversamplingFactor >= 4;
            auto& oversampler = use4x ? nonlinearOversamplers4x[(size_t) mode]
                                      : nonlinearOversamplers2x[(size_t) mode];
            auto& nonlinearBuffer = nonlinearBuffers[(size_t) mode];
            const int preparedAudioChannels = numChannels;
            for (int offset = 0; offset < b.getNumSamples(); offset += nonlinearMaxBlock)
            {
                const int blockSamples = juce::jmin(nonlinearMaxBlock, b.getNumSamples() - offset);
                for (int ch = 0; ch < preparedAudioChannels; ++ch)
                {
                    auto* audio = nonlinearBuffer.getWritePointer(ch);
                    auto* gain = nonlinearBuffer.getWritePointer(preparedAudioChannels + ch);
                    if (ch < b.getNumChannels())
                    {
                        std::copy_n(b.getReadPointer(ch) + offset, blockSamples, audio);
                        std::copy_n(gainScratch.getReadPointer(ch) + offset, blockSamples, gain);
                    }
                    else
                    {
                        std::fill_n(audio, blockSamples, 0.0f);
                        std::fill_n(gain, blockSamples, 1.0f);
                    }
                }

                oversampler.process(nonlinearBuffer, blockSamples,
                    [&](auto& oversampled, int oversampledSamples) noexcept
                    {
                        const double rateScale = (double)oversampler.getFactor();
                        for (int ch = 0; ch < b.getNumChannels(); ++ch)
                        {
                            auto* audio = oversampled.getWritePointer(ch);
                            const auto* gain = oversampled.getReadPointer(preparedAudioChannels + ch);
                            for (int i = 0; i < oversampledSamples; ++i)
                                audio[i] = processGainCell(ch, audio[i], gain[i], rateScale);
                        }
                    });

                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    std::copy_n(nonlinearBuffer.getReadPointer(ch), blockSamples,
                                b.getWritePointer(ch) + offset);
            }
            auto& alignmentDelay = use4x ? oversampling4xAlignmentDelays[(size_t) mode]
                                         : oversampling2xAlignmentDelays[(size_t) mode];
            alignmentDelay.process(b);
        }
        else
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                auto* audio = b.getWritePointer(ch);
                const auto* gain = gainScratch.getReadPointer(ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    audio[i] = processGainCell(ch, audio[i], gain[i], 1.0);
            }
            nativeModeDelays[(size_t) mode].process(b);
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
    juce::AudioBuffer<float> lowBand, highLowPassed, gainScratch;
    std::array<patina::Oversampler<float>, 4> nonlinearOversamplers2x;
    std::array<patina::Oversampler<float>, 4> nonlinearOversamplers4x;
    std::array<patina::compat::AudioBuffer<float>, 4> nonlinearBuffers;
    std::array<FixedDelay, 4> oversampling2xAlignmentDelays;
    std::array<FixedDelay, 4> oversampling4xAlignmentDelays;
    std::array<FixedDelay, 4> nativeModeDelays;
    FixedDelay inputPathDelay;
    juce::dsp::StateVariableTPTFilter<float> lowSplit, highSplit, sidechainHigh;
    float lowSplitCutoffHz = 120.0f;
    float highSplitCutoffHz = 8000.0f;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int allocatedSamples = 0;
    int nonlinearMaxBlock = 1;
    int nonlinearLatencySamples = 0;
    Mode currentMode = FET, requestedMode = FET, oldMode = FET;
    int xfadeRemaining = 0;
    float smoothedGrDb = 0.0f;
    std::array<float, 2> smoothedGrDbByChannel {};
    float autoGainDb = 0.0f;
    bool previousStereoChain = false;
    bool stereoChainStateInitialised = false;
    int stereoLinkFadeRemaining = 0;
    std::array<float, 2> stereoLinkFadeStartGrDb {};
    std::array<std::array<float, 2>, 4> lastDetectorGrDbByMode {};
    bool previousOversampling = true;
    int previousOversamplingFactor = 2;
    bool previousDriveActive = false;
    std::array<float, 4> modeAverageGr {};
    float transitionTrimDb = 0.0f;
    std::atomic<bool> photoResetRequested { false };
    std::atomic<float> displayThresholdDb { -18.0f };
    std::atomic<float> displayGraphGainDb { 0.0f };
};
