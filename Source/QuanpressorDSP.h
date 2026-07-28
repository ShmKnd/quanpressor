#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <patina.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

class QuanpressorDSP
{
public:
    enum Mode { FET = 0, OPTO, VCA, VARIMU };

    static constexpr double modeCrossfadeSeconds = 0.030;
    static constexpr double stereoLinkCrossfadeSeconds = 0.005;
    static constexpr double qualityCrossfadeSeconds = 0.010;
    static constexpr double discreteControlCrossfadeSeconds = 0.005;

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

    static float fetAttackMilliseconds(float parameterValue) noexcept
    {
        const float position = juce::jlimit(
            0.0f, 1.0f,
            (parameterValue - 0.02f) / (100.0f - 0.02f));
        return juce::jmap(std::sqrt(position), 0.02f, 0.8f);
    }

    static float variableMuGainReductionDb(float overDb) noexcept
    {
        overDb = juce::jmax(0.0f, overDb);
        constexpr float kneeDb = 6.0f;
        const float effectiveOver = overDb < kneeDb
            ? overDb * overDb / (2.0f * kneeDb)
            : overDb - kneeDb * 0.5f;

        constexpr float initialSlope = 2.0f / 3.0f;
        constexpr float limitingSlope = 0.08f;
        constexpr float transitionDb = 4.5f;
        const float outputOver = limitingSlope * effectiveOver
            + (initialSlope - limitingSlope) * transitionDb
                * (1.0f - std::exp(-effectiveOver / transitionDb));
        return juce::jmax(0.0f, effectiveOver - outputOver);
    }

    static float variableMuOverDbForGainReduction(float grDb) noexcept
    {
        grDb = juce::jmax(0.0f, grDb);
        float lo = 0.0f;
        float hi = 96.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (variableMuGainReductionDb(mid) < grDb)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    void prepare(double sr, int maxBlock, int channels)
    {
        sampleRate = juce::jmax(1.0, sr);
        numChannels = juce::jmax(1, channels);
        nonlinearMaxBlock = juce::jmax(1, maxBlock);
        allocatedSamples = nonlinearMaxBlock;

        const patina::ProcessSpec spec {
            sampleRate, nonlinearMaxBlock, numChannels
        };
        for (auto& quality : gainEngines)
            for (auto& engine : quality)
                engine.prepare(spec);

        nonlinearLatencySamples = 0;
        for (size_t mode = 0; mode < nonlinearBuffers.size(); ++mode)
        {
            nonlinearBuffers[mode].setSize(numChannels, nonlinearMaxBlock);
            nonlinearOversamplers2x[mode].prepare(
                nonlinearMaxBlock, numChannels,
                patina::OversamplingFactor::x2, patina::FilterQuality::High);
            nonlinearOversamplers4x[mode].prepare(
                nonlinearMaxBlock, numChannels,
                patina::OversamplingFactor::x4, patina::FilterQuality::High);
            resetNonlinearOversampler(nonlinearOversamplers2x[mode]);
            resetNonlinearOversampler(nonlinearOversamplers4x[mode]);
            nonlinearLatencySamples = juce::jmax(
                nonlinearLatencySamples,
                nonlinearOversamplers4x[mode].getLatency());
        }

        inputPathDelay.prepare(numChannels, nonlinearLatencySamples);
        for (auto& delay : nativeModeDelays)
            delay.prepare(numChannels, nonlinearLatencySamples);
        for (size_t mode = 0; mode < nonlinearBuffers.size(); ++mode)
        {
            oversampling2xAlignmentDelays[mode].prepare(
                numChannels,
                nonlinearLatencySamples - nonlinearOversamplers2x[mode].getLatency());
            oversampling4xAlignmentDelays[mode].prepare(
                numChannels,
                nonlinearLatencySamples - nonlinearOversamplers4x[mode].getLatency());
        }

        for (size_t quality = 0;
             quality < qualityOutputTransformers.size();
             ++quality)
        {
            const int factor = quality == nativeQuality ? 1
                : quality == quality2x ? 2 : 4;
            for (auto& modeTransformers :
                 qualityOutputTransformers[quality])
            {
                modeTransformers.resize(static_cast<size_t>(numChannels));
                for (auto& transformer : modeTransformers)
                    transformer.prepare(1, sampleRate * factor);
            }
        }

        const juce::dsp::ProcessSpec juceSpec {
            sampleRate,
            static_cast<juce::uint32>(nonlinearMaxBlock),
            static_cast<juce::uint32>(numChannels)
        };
        lowSplit.prepare(juceSpec);
        highSplit.prepare(juceSpec);
        sidechainHigh.prepare(juceSpec);
        lowSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        highSplit.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        sidechainHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        lowSplitCutoffHz = 120.0f;
        highSplitCutoffHz = 8000.0f;
        lowSplit.setCutoffFrequency(lowSplitCutoffHz);
        highSplit.setCutoffFrequency(highSplitCutoffHz);
        sidechainHigh.setCutoffFrequency(120.0f);

        for (auto* scratch : {
                 &inputReference, &detectorBuffer, &modeInput, &oldWet,
                 &qualityOldWet, &lowBand, &highLowPassed, &gainScratch
             })
        {
            scratch->setSize(numChannels, allocatedSamples, false, true, false);
        }
        controlScratch.setSize(controlCount, allocatedSamples, false, true, false);

        for (size_t index = 0; index < controlSmoothers.size(); ++index)
            controlSmoothers[index].reset(sampleRate, smoothingSeconds(index));

        reset();
    }

    void reset()
    {
        for (auto& quality : gainEngines)
            for (auto& engine : quality)
                engine.reset();
        for (auto& oversampler : nonlinearOversamplers2x)
            resetNonlinearOversampler(oversampler);
        for (auto& oversampler : nonlinearOversamplers4x)
            resetNonlinearOversampler(oversampler);
        for (auto& delay : oversampling2xAlignmentDelays)
            delay.reset();
        for (auto& delay : oversampling4xAlignmentDelays)
            delay.reset();
        for (auto& delay : nativeModeDelays)
            delay.reset();
        inputPathDelay.reset();
        for (auto& quality : qualityOutputTransformers)
            for (auto& modeTransformers : quality)
                for (auto& transformer : modeTransformers)
                    transformer.reset();
        for (auto& history : nonlinearControlHistory)
            history.reset();
        lowSplit.reset();
        highSplit.reset();
        sidechainHigh.reset();

        for (auto& modeStates : detectorStates)
            for (auto& state : modeStates)
                state.reset();
        for (auto& state : linkedDetectorStates)
            state.reset();

        currentMode = requestedMode = oldMode = FET;
        xfadeRemaining = 0;
        activeQuality = requestedQuality = oldQuality = nativeQuality;
        qualityFadeRemaining = 0;
        smoothedGrDb = 0.0f;
        smoothedGrDbByChannel.fill(0.0f);
        instantaneousGrDb = 0.0f;
        autoGainDb = 0.0f;
        previousStereoChain = false;
        stereoChainStateInitialised = false;
        stereoLinkFadeRemaining = 0;
        stereoLinkFadeStartGrDb.fill(0.0f);
        for (auto& modeGr : lastDetectorGrDbByMode)
            modeGr.fill(0.0f);
        transitionTrimDb = 0.0f;
        photoResetFadeRemaining = 0;
        photoResetFadeStartGrDb.fill(0.0f);
        controlsInitialised = false;
        discreteSelectionsInitialised = false;
        activeFetRatioIndex = 0;
        activeVarMuTimeConstant = 1;
        displayGraphGainDb.store(0.0f, std::memory_order_relaxed);
    }

    void requestPhotoCellReset() noexcept
    {
        photoResetRequested.store(true, std::memory_order_release);
    }

    void process(juce::AudioBuffer<float>& buffer, const Controls& controls)
    {
        const int totalSamples = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        if (totalSamples <= 0 || channels <= 0)
            return;

        jassert(channels <= numChannels);
        if (channels > numChannels)
            return;

        for (int offset = 0; offset < totalSamples; offset += nonlinearMaxBlock)
        {
            const int samples = juce::jmin(nonlinearMaxBlock, totalSamples - offset);
            juce::AudioBuffer<float> chunk(
                buffer.getArrayOfWritePointers(), channels, offset, samples);
            processChunk(chunk, controls);
        }
    }

    float getGrDb() const noexcept
    {
        return smoothedGrDb;
    }

    float getInstantaneousGrDb() const noexcept
    {
        return instantaneousGrDb;
    }

    float getGrDbForChannel(int channel) const noexcept
    {
        return smoothedGrDbByChannel[
            static_cast<size_t>(juce::jlimit(0, 1, channel))];
    }

    float getEffectiveThresholdDb() const noexcept
    {
        return displayThresholdDb.load(std::memory_order_relaxed);
    }

    float getGraphGainDb() const noexcept
    {
        return displayGraphGainDb.load(std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept
    {
        return nonlinearLatencySamples;
    }

    int getModeCrossfadeRemainingSamples() const noexcept
    {
        return xfadeRemaining;
    }

    float getModeTransitionTrimDb() const noexcept
    {
        return transitionTrimDb;
    }

    int getStereoLinkCrossfadeRemainingSamples() const noexcept
    {
        return stereoLinkFadeRemaining;
    }

    int getQualityCrossfadeRemainingSamples() const noexcept
    {
        return qualityFadeRemaining;
    }

    int getDiscreteControlCrossfadeRemainingSamples(Mode mode) const noexcept
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        int remaining = linkedDetectorStates[modeIndex].discreteFadeRemaining;
        for (const auto& state : detectorStates[modeIndex])
            remaining = juce::jmax(remaining, state.discreteFadeRemaining);
        return remaining;
    }

private:
    enum ControlIndex
    {
        inputControl = 0,
        outputControl,
        makeupControl,
        mixControl,
        driveControl,
        thresholdControl,
        ratioControl,
        kneeControl,
        attackControl,
        releaseControl,
        controlCount
    };

    enum Quality
    {
        nativeQuality = 0,
        quality2x,
        quality4x,
        qualityCount
    };

    struct DetectorState
    {
        float envelope = 0.0f;
        float previousEnvelope = 0.0f;
        float fast = 0.0f;
        float slow = 0.0f;
        float longTerm = 0.0f;
        float sustain = 0.0f;
        float longSustain = 0.0f;
        float controlDb = 0.0f;
        float lastGain = 1.0f;
        float discreteFadeStartDb = 0.0f;
        int discreteFadeRemaining = 0;

        void reset() noexcept
        {
            envelope = 0.0f;
            previousEnvelope = 0.0f;
            fast = 0.0f;
            slow = 0.0f;
            longTerm = 0.0f;
            sustain = 0.0f;
            longSustain = 0.0f;
            controlDb = 0.0f;
            lastGain = 1.0f;
            discreteFadeStartDb = 0.0f;
            discreteFadeRemaining = 0;
        }
    };

    struct EngineSet
    {
        PhotoCompressor photo;
        FetCompressor fet;
        VariableMuCompressor mu;
        VcaCompressor vca;

        void prepare(const patina::ProcessSpec& spec)
        {
            photo.prepare(spec);
            fet.prepare(spec);
            mu.prepare(spec);
            vca.prepare(spec);
        }

        void reset()
        {
            photo.reset();
            fet.reset();
            mu.reset();
            vca.reset();
        }
    };

    class FixedDelay
    {
    public:
        void prepare(int channels, int samples)
        {
            delaySamples = juce::jmax(0, samples);
            storage.setSize(
                juce::jmax(1, channels),
                juce::jmax(1, delaySamples),
                false, true, false);
            writePositions.assign(
                static_cast<size_t>(juce::jmax(1, channels)), 0);
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

            const int channels = juce::jmin(
                buffer.getNumChannels(), storage.getNumChannels());
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* samples = buffer.getWritePointer(channel);
                auto* delayed = storage.getWritePointer(channel);
                int position = writePositions[static_cast<size_t>(channel)];
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const float input = samples[i];
                    samples[i] = delayed[position];
                    delayed[position] = input;
                    if (++position >= delaySamples)
                        position = 0;
                }
                writePositions[static_cast<size_t>(channel)] = position;
            }
        }

    private:
        juce::AudioBuffer<float> storage;
        std::vector<int> writePositions;
        int delaySamples = 0;
    };

    static constexpr int controlHistoryCapacity = 128;

    struct ControlHistory
    {
        void reset() noexcept
        {
            for (auto& channel : gain)
                channel.fill(1.0f);
            drive.fill(0.0f);
            initialGain.fill(1.0f);
            initialDrive = 0.0f;
            writePosition = 0;
            validSamples = 0;
            initialised = false;
        }

        void prime(float leftGain, float rightGain, float driveValue) noexcept
        {
            if (initialised)
                return;

            initialGain = {
                juce::jlimit(0.001f, 1.0f, leftGain),
                juce::jlimit(0.001f, 1.0f, rightGain)
            };
            initialDrive = juce::jlimit(0.0f, 18.0f, driveValue);
            for (size_t channel = 0; channel < gain.size(); ++channel)
                gain[channel].fill(initialGain[channel]);
            drive.fill(initialDrive);
            initialised = true;
        }

        float gainBeforeBlock(int channel, int relativeIndex) const noexcept
        {
            const int channelIndex = juce::jlimit(0, 1, channel);
            const int distance = juce::jmax(1, -relativeIndex);
            if (!initialised || distance > validSamples)
                return initialGain[static_cast<size_t>(channelIndex)];

            int index = writePosition - distance;
            if (index < 0)
                index += controlHistoryCapacity;
            return gain[static_cast<size_t>(channelIndex)][
                static_cast<size_t>(index)];
        }

        float driveBeforeBlock(int relativeIndex) const noexcept
        {
            const int distance = juce::jmax(1, -relativeIndex);
            if (!initialised || distance > validSamples)
                return initialDrive;

            int index = writePosition - distance;
            if (index < 0)
                index += controlHistoryCapacity;
            return drive[static_cast<size_t>(index)];
        }

        void append(
            const juce::AudioBuffer<float>& gainBuffer,
            const float* driveValues,
            int channels,
            int samples) noexcept
        {
            for (int sample = 0; sample < samples; ++sample)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    const int sourceChannel = juce::jmin(
                        channel, juce::jmax(0, channels - 1));
                    gain[static_cast<size_t>(channel)][
                        static_cast<size_t>(writePosition)]
                        = juce::jlimit(
                            0.001f, 1.0f,
                            gainBuffer.getSample(sourceChannel, sample));
                }
                drive[static_cast<size_t>(writePosition)]
                    = juce::jlimit(0.0f, 18.0f, driveValues[sample]);
                if (++writePosition >= controlHistoryCapacity)
                    writePosition = 0;
                validSamples = juce::jmin(
                    controlHistoryCapacity, validSamples + 1);
            }
        }

        std::array<std::array<float, controlHistoryCapacity>, 2> gain {};
        std::array<float, controlHistoryCapacity> drive {};
        std::array<float, 2> initialGain { 1.0f, 1.0f };
        float initialDrive = 0.0f;
        int writePosition = 0;
        int validSamples = 0;
        bool initialised = false;
    };

    static constexpr std::array<float, 4> fetFeedbackAmounts {
        3.0f, 7.0f, 11.0f, 19.0f
    };

    static constexpr std::array<float, 4> fetKneeWidths {
        7.0f, 5.5f, 4.0f, 2.5f
    };

    static constexpr std::array<float, 4> fetTransientAmounts {
        0.20f, 0.35f, 0.50f, 0.75f
    };

    static constexpr std::array<std::array<float, 4>, 6> variableMuTimeConstants {{
        {{ 0.2f, 300.0f, 300.0f, 300.0f }},
        {{ 0.2f, 800.0f, 800.0f, 800.0f }},
        {{ 0.4f, 2000.0f, 2000.0f, 2000.0f }},
        {{ 0.4f, 5000.0f, 5000.0f, 5000.0f }},
        {{ 0.4f, 2000.0f, 10000.0f, 10000.0f }},
        {{ 0.2f, 300.0f, 10000.0f, 25000.0f }}
    }};

    static double smoothingSeconds(size_t index) noexcept
    {
        switch (index)
        {
            case driveControl: return 0.025;
            case attackControl:
            case releaseControl: return 0.020;
            default: return 0.012;
        }
    }

    static float controlValue(const Controls& controls, size_t index) noexcept
    {
        switch (index)
        {
            case inputControl: return controls.inputDb;
            case outputControl: return controls.outputDb;
            case makeupControl: return controls.autoLevel ? 0.0f : controls.makeupDb;
            case mixControl: return juce::jlimit(0.0f, 1.0f, controls.mix);
            case driveControl: return juce::jlimit(0.0f, 18.0f, controls.driveDb);
            case thresholdControl: return juce::jlimit(-48.0f, 0.0f, controls.thresholdDb);
            case ratioControl: return juce::jlimit(1.5f, 20.0f, controls.ratio);
            case kneeControl: return juce::jlimit(0.0f, 18.0f, controls.kneeDb);
            case attackControl: return juce::jlimit(0.02f, 100.0f, controls.attackMs);
            case releaseControl: return juce::jlimit(20.0f, 1200.0f, controls.releaseMs);
            default: return 0.0f;
        }
    }

    void fillControlScratch(const Controls& controls, int samples) noexcept
    {
        if (!controlsInitialised)
        {
            for (size_t index = 0; index < controlSmoothers.size(); ++index)
            {
                const float value = controlValue(controls, index);
                controlSmoothers[index].setCurrentAndTargetValue(value);
            }
            controlsInitialised = true;
        }
        else
        {
            for (size_t index = 0; index < controlSmoothers.size(); ++index)
                controlSmoothers[index].setTargetValue(controlValue(controls, index));
        }

        for (int sample = 0; sample < samples; ++sample)
            for (size_t index = 0; index < controlSmoothers.size(); ++index)
                controlScratch.setSample(
                    static_cast<int>(index), sample,
                    controlSmoothers[index].getNextValue());
    }

    int modeCrossfadeSamples() const noexcept
    {
        return juce::jmax(
            1, juce::roundToInt(modeCrossfadeSeconds * sampleRate));
    }

    int stereoLinkCrossfadeSamples() const noexcept
    {
        return juce::jmax(
            1, juce::roundToInt(stereoLinkCrossfadeSeconds * sampleRate));
    }

    int qualityCrossfadeSamples() const noexcept
    {
        return juce::jmax(
            1, juce::roundToInt(qualityCrossfadeSeconds * sampleRate));
    }

    int discreteControlCrossfadeSamples() const noexcept
    {
        return juce::jmax(
            1, juce::roundToInt(
                discreteControlCrossfadeSeconds * sampleRate));
    }

    void beginDiscreteControlTransition(Mode mode) noexcept
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        for (auto& state : detectorStates[modeIndex])
        {
            state.discreteFadeStartDb = state.controlDb;
            state.discreteFadeRemaining = discreteControlCrossfadeSamples();
        }
        auto& linkedState = linkedDetectorStates[modeIndex];
        linkedState.discreteFadeStartDb = linkedState.controlDb;
        linkedState.discreteFadeRemaining = discreteControlCrossfadeSamples();
    }

    void updateDiscreteSelections(const Controls& controls) noexcept
    {
        const int requestedFetRatio = fetRatioIndex(controls.ratio);
        const int requestedVarMuTimeConstant = juce::jlimit(
            0, 5, controls.varMuSpeed);
        if (!discreteSelectionsInitialised)
        {
            activeFetRatioIndex = requestedFetRatio;
            activeVarMuTimeConstant = requestedVarMuTimeConstant;
            discreteSelectionsInitialised = true;
            return;
        }

        if (requestedFetRatio != activeFetRatioIndex)
        {
            activeFetRatioIndex = requestedFetRatio;
            beginDiscreteControlTransition(FET);
        }
        if (requestedVarMuTimeConstant != activeVarMuTimeConstant)
        {
            activeVarMuTimeConstant = requestedVarMuTimeConstant;
            beginDiscreteControlTransition(VARIMU);
        }
    }

    static int qualityForControls(const Controls& controls) noexcept
    {
        if (!controls.oversampling)
            return nativeQuality;
        return controls.oversamplingFactor >= 4 ? quality4x : quality2x;
    }

    void resetNonlinearOversampler(
        patina::Oversampler<float>& oversampler) noexcept
    {
        oversampler.reset();
    }

    void resetMode(Mode mode)
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        for (auto& quality : gainEngines)
            quality[modeIndex].reset();
        detectorStates[modeIndex][0].reset();
        detectorStates[modeIndex][1].reset();
        linkedDetectorStates[modeIndex].reset();
        resetNonlinearOversampler(nonlinearOversamplers2x[modeIndex]);
        resetNonlinearOversampler(nonlinearOversamplers4x[modeIndex]);
        oversampling2xAlignmentDelays[modeIndex].reset();
        oversampling4xAlignmentDelays[modeIndex].reset();
        nativeModeDelays[modeIndex].reset();
        for (auto& quality : qualityOutputTransformers)
            for (auto& transformer : quality[modeIndex])
                transformer.reset();
        nonlinearControlHistory[modeIndex].reset();
        lastDetectorGrDbByMode[modeIndex].fill(0.0f);
    }

    void resetQualityPath(Mode mode, int quality)
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        gainEngines[static_cast<size_t>(quality)][modeIndex].reset();
        for (auto& transformer :
             qualityOutputTransformers[static_cast<size_t>(quality)][modeIndex])
        {
            transformer.reset();
        }
        if (quality == nativeQuality)
            nativeModeDelays[modeIndex].reset();
        else if (quality == quality2x)
        {
            resetNonlinearOversampler(nonlinearOversamplers2x[modeIndex]);
            oversampling2xAlignmentDelays[modeIndex].reset();
        }
        else
        {
            resetNonlinearOversampler(nonlinearOversamplers4x[modeIndex]);
            oversampling4xAlignmentDelays[modeIndex].reset();
        }
    }

    static void copyBuffer(
        juce::AudioBuffer<float>& destination,
        const juce::AudioBuffer<float>& source,
        int channels,
        int samples) noexcept
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            juce::FloatVectorOperations::copy(
                destination.getWritePointer(channel),
                source.getReadPointer(channel),
                samples);
        }
    }

    static double bufferEnergy(
        const juce::AudioBuffer<float>& buffer,
        int channels,
        int samples) noexcept
    {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* data = buffer.getReadPointer(channel);
            for (int i = 0; i < samples; ++i)
                sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
        }
        return sum;
    }

    static float rmsDb(double energy, int count) noexcept
    {
        const double mean = energy / static_cast<double>(juce::jmax(1, count));
        return juce::Decibels::gainToDecibels(
            static_cast<float>(std::sqrt(juce::jmax(mean, 1.0e-20))),
            -100.0f);
    }

    float coefficientMs(float milliseconds) const noexcept
    {
        milliseconds = juce::jmax(0.001f, milliseconds);
        return 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate * milliseconds * 0.001));
    }

    static float smoothAsymmetric(
        float target,
        float current,
        float attackCoefficient,
        float releaseCoefficient) noexcept
    {
        const float coefficient = target > current
            ? attackCoefficient : releaseCoefficient;
        return current + coefficient * (target - current);
    }

    static float effectiveOverDb(float overDb, float kneeDb) noexcept
    {
        kneeDb = juce::jmax(0.0f, kneeDb);
        if (kneeDb <= 0.0f)
            return juce::jmax(0.0f, overDb);
        if (overDb <= -0.5f * kneeDb)
            return 0.0f;
        if (overDb >= 0.5f * kneeDb)
            return overDb;
        const float position = overDb + 0.5f * kneeDb;
        return position * position / (2.0f * kneeDb);
    }

    static float feedForwardGainReductionDb(
        float levelDb,
        float thresholdDb,
        float ratio,
        float kneeDb) noexcept
    {
        ratio = juce::jmax(1.0f, ratio);
        const float over = effectiveOverDb(levelDb - thresholdDb, kneeDb);
        return -over * (1.0f - 1.0f / ratio);
    }

    float updateFetDetector(
        DetectorState& state,
        float feedbackLevel,
        float thresholdDb,
        float attackParameterMs,
        float releaseParameterMs,
        int ratioIndex,
        bool autoRelease) noexcept
    {
        const float attackMs = fetAttackMilliseconds(attackParameterMs);
        float releaseMs = juce::jmap(
            juce::jlimit(0.0f, 1.0f,
                         (releaseParameterMs - 20.0f) / 1180.0f),
            50.0f, 1100.0f);
        if (autoRelease)
        {
            const float depth = juce::jlimit(
                0.0f, 1.0f, -state.controlDb / 24.0f);
            releaseMs = juce::jmap(depth * depth, 65.0f, 700.0f);
        }

        state.previousEnvelope = state.envelope;
        state.envelope = smoothAsymmetric(
            juce::jmax(0.0f, feedbackLevel),
            state.envelope,
            coefficientMs(attackMs),
            coefficientMs(releaseMs));
        const float levelDb = juce::Decibels::gainToDecibels(
            juce::jmax(state.envelope, 1.0e-10f), -120.0f);
        const float effectiveOver = effectiveOverDb(
            levelDb - thresholdDb,
            fetKneeWidths[static_cast<size_t>(ratioIndex)]);
        const float relativeRise = juce::jmax(
            0.0f,
            state.envelope - state.previousEnvelope)
            / juce::jmax(state.envelope, 1.0e-6f);
        const float transientOvershoot = juce::jmin(
            2.5f,
            effectiveOver * relativeRise
                * fetTransientAmounts[static_cast<size_t>(ratioIndex)]);
        const float reduction = juce::jlimit(
            0.0f, 48.0f,
            effectiveOver * fetFeedbackAmounts[static_cast<size_t>(ratioIndex)]
                + transientOvershoot);
        state.controlDb = -reduction;
        state.lastGain = juce::Decibels::decibelsToGain(state.controlDb);
        return state.controlDb;
    }

    float updateOptoDetector(
        DetectorState& state,
        float rectifierInput,
        float thresholdDb,
        float ratio,
        float attackParameterMs,
        float releaseParameterMs) noexcept
    {
        state.envelope = smoothAsymmetric(
            juce::jmax(0.0f, rectifierInput),
            state.envelope,
            coefficientMs(0.08f),
            coefficientMs(1.5f));

        const float levelDb = juce::Decibels::gainToDecibels(
            juce::jmax(state.envelope, 1.0e-10f), -120.0f);
        const float opticalRatio = juce::jmap(
            juce::jlimit(1.5f, 20.0f, ratio),
            1.5f, 20.0f, 2.2f, 10.0f);
        const float targetDepth = -feedForwardGainReductionDb(
            levelDb, thresholdDb, opticalRatio, 7.0f);
        const float attackPosition = juce::jlimit(
            0.0f, 1.0f,
            (attackParameterMs - 0.02f) / (100.0f - 0.02f));
        const float opticalAttackMs = juce::jmap(
            std::sqrt(attackPosition), 5.0f, 35.0f);
        const float releasePosition = juce::jlimit(
            0.0f, 1.0f,
            (releaseParameterMs - 20.0f) / 1180.0f);
        const float fastReleaseMs = juce::jmap(
            releasePosition, 80.0f, 450.0f);
        const float slowReleaseMs = juce::jmap(
            releasePosition, 900.0f, 4200.0f);

        state.fast = smoothAsymmetric(
            targetDepth,
            state.fast,
            coefficientMs(opticalAttackMs),
            coefficientMs(fastReleaseMs));
        state.slow = smoothAsymmetric(
            targetDepth,
            state.slow,
            coefficientMs(220.0f),
            coefficientMs(slowReleaseMs));

        const float durationTarget = targetDepth > 1.0f ? 1.0f : 0.0f;
        state.sustain = smoothAsymmetric(
            durationTarget,
            state.sustain,
            coefficientMs(450.0f),
            coefficientMs(1200.0f));
        const float depthWeight = juce::jlimit(
            0.0f, 1.0f, targetDepth / 18.0f);
        const float memoryWeight = juce::jlimit(
            0.08f, 0.78f,
            0.08f + 0.70f * state.sustain * depthWeight);
        const float reduction = juce::jlimit(
            0.0f, 48.0f,
            state.fast * (1.0f - memoryWeight) + state.slow * memoryWeight);
        state.controlDb = -reduction;
        state.lastGain = juce::Decibels::decibelsToGain(state.controlDb);
        return state.controlDb;
    }

    float updateVcaDetector(
        DetectorState& state,
        float detectorLevel,
        float thresholdDb,
        float ratio,
        float kneeDb,
        float attackMs,
        float releaseMs,
        bool autoRelease) noexcept
    {
        const float square = detectorLevel * detectorLevel;
        const float mappedAttackMs = juce::jlimit(0.1f, 80.0f, attackMs);
        const float mappedReleaseMs = juce::jlimit(50.0f, 1200.0f, releaseMs);
        state.envelope = smoothAsymmetric(
            square,
            state.envelope,
            coefficientMs(mappedAttackMs),
            coefficientMs(mappedReleaseMs));
        const float levelDb = juce::Decibels::gainToDecibels(
            std::sqrt(juce::jmax(state.envelope, 1.0e-20f)), -120.0f);
        const float targetDb = feedForwardGainReductionDb(
            levelDb, thresholdDb, ratio, kneeDb);

        if (autoRelease)
        {
            const bool compressing = targetDb < state.controlDb;
            const float fastTime = compressing ? mappedAttackMs : 100.0f;
            const float slowTime = compressing ? mappedAttackMs : 900.0f;
            state.fast += coefficientMs(fastTime) * (targetDb - state.fast);
            state.slow += coefficientMs(slowTime) * (targetDb - state.slow);
            const float sustainTarget = juce::jlimit(
                0.0f, 1.0f, -targetDb / 18.0f);
            state.sustain += coefficientMs(
                sustainTarget > state.sustain ? 500.0f : 1400.0f)
                * (sustainTarget - state.sustain);
            state.controlDb = juce::jmap(
                state.sustain * state.sustain,
                state.fast,
                state.slow);
        }
        else
        {
            state.controlDb = targetDb;
            state.fast = targetDb;
            state.slow = targetDb;
        }

        state.controlDb = juce::jlimit(-60.0f, 0.0f, state.controlDb);
        state.lastGain = juce::Decibels::decibelsToGain(state.controlDb);
        return state.controlDb;
    }

    float updateVariableMuDetector(
        DetectorState& state,
        float feedbackLevel,
        float thresholdDb,
        float ratio,
        float kneeDb,
        float attackParameterMs,
        float releaseParameterMs,
        int timeConstant) noexcept
    {
        const int selectedTimeConstant = juce::jlimit(0, 5, timeConstant);
        const auto& selected = variableMuTimeConstants[
            static_cast<size_t>(selectedTimeConstant)];

        // Attack and Release remain compatibility trims around the six physical
        // time-constant networks. The selector remains the dominant timing control.
        const float attackTrim = juce::jmap(
            juce::jlimit(0.0f, 1.0f,
                         (attackParameterMs - 0.02f) / (100.0f - 0.02f)),
            0.75f, 1.25f);
        const float releaseTrim = juce::jmap(
            juce::jlimit(0.0f, 1.0f,
                         (releaseParameterMs - 20.0f) / 1180.0f),
            0.75f, 1.25f);
        const float rectified = juce::jmax(0.0f, feedbackLevel);
        const bool programDependent = selectedTimeConstant >= 4;
        const float fastReleaseMs = (programDependent
            ? selected[1] : selected[1] * 0.55f) * releaseTrim;
        const float slowReleaseMs = (programDependent
            ? selected[2] : selected[1] * 2.4f) * releaseTrim;
        const float sustainedReleaseMs = selected[3] * releaseTrim;
        const float slowAttackMs = programDependent
            ? 650.0f : selected[0] * 3.0f;
        const float sustainedAttackMs = programDependent
            ? 2500.0f : selected[0] * 3.0f;
        state.fast = smoothAsymmetric(
            rectified,
            state.fast,
            coefficientMs(selected[0] * attackTrim),
            coefficientMs(fastReleaseMs));
        state.slow = smoothAsymmetric(
            rectified,
            state.slow,
            coefficientMs(slowAttackMs * attackTrim),
            coefficientMs(slowReleaseMs));
        state.longTerm = smoothAsymmetric(
            rectified,
            state.longTerm,
            coefficientMs(sustainedAttackMs * attackTrim),
            coefficientMs(sustainedReleaseMs));
        const float sustainTarget = rectified
            > juce::Decibels::decibelsToGain(thresholdDb) * 0.8f
                ? 1.0f : 0.0f;
        state.sustain = smoothAsymmetric(
            sustainTarget,
            state.sustain,
            coefficientMs(650.0f),
            coefficientMs(slowReleaseMs));
        state.longSustain = smoothAsymmetric(
            sustainTarget,
            state.longSustain,
            coefficientMs(2500.0f),
            coefficientMs(sustainedReleaseMs));
        const float depth = juce::jlimit(
            0.0f, 1.0f, -state.controlDb / 24.0f);
        if (programDependent)
        {
            const float multiplePeakWeight = juce::jlimit(
                0.0f,
                selectedTimeConstant == 4 ? 0.75f : 0.35f,
                (selectedTimeConstant == 4 ? 0.75f : 0.35f)
                    * state.sustain);
            const float sustainedWeight = selectedTimeConstant == 5
                ? juce::jlimit(
                    0.0f, 0.50f,
                    0.50f * state.longSustain)
                : 0.0f;
            const float fastWeight = juce::jmax(
                0.15f, 1.0f - multiplePeakWeight - sustainedWeight);
            const float normalisation = fastWeight
                + multiplePeakWeight + sustainedWeight;
            state.envelope = (
                state.fast * fastWeight
                + state.slow * multiplePeakWeight
                + state.longTerm * sustainedWeight)
                / normalisation;
        }
        else
        {
            const float memoryWeight = juce::jlimit(
                0.08f, 0.60f,
                0.08f + 0.52f * state.sustain * depth);
            state.envelope = state.fast * (1.0f - memoryWeight)
                + state.slow * memoryWeight;
        }

        const float levelDb = juce::Decibels::gainToDecibels(
            juce::jmax(state.envelope, 1.0e-10f), -120.0f);
        const float loopStrength = juce::jmap(
            juce::jlimit(1.5f, 20.0f, ratio),
            1.5f, 20.0f, 0.65f, 1.35f);
        const float overDb = effectiveOverDb(
            levelDb - thresholdDb,
            juce::jlimit(2.0f, 12.0f, kneeDb));
        const double controlVoltage = juce::jlimit(
            0.0, 2.2,
            static_cast<double>(overDb * loopStrength / 20.0f));
        const double tubeGain = TubeTriode::variableMuGain(
            controlVoltage, TubeTriode::Tube6386());
        state.controlDb = juce::jlimit(
            -60.0f, 0.0f,
            juce::Decibels::gainToDecibels(
                static_cast<float>(tubeGain), -60.0f));
        state.lastGain = juce::Decibels::decibelsToGain(state.controlDb);
        return state.controlDb;
    }

    float updateDetector(
        Mode mode,
        DetectorState& state,
        float detectorSample,
        float thresholdDb,
        float ratio,
        float kneeDb,
        float attackMs,
        float releaseMs,
        bool autoRelease) noexcept
    {
        detectorSample = std::isfinite(detectorSample) ? detectorSample : 0.0f;
        switch (mode)
        {
            case FET:
                updateFetDetector(
                    state,
                    std::abs(detectorSample) * state.lastGain,
                    thresholdDb,
                    attackMs,
                    releaseMs,
                    activeFetRatioIndex,
                    autoRelease);
                break;
            case OPTO:
                return updateOptoDetector(
                    state,
                    std::abs(detectorSample),
                    thresholdDb,
                    ratio,
                    attackMs,
                    releaseMs);
            case VCA:
                return updateVcaDetector(
                    state,
                    std::abs(detectorSample),
                    thresholdDb,
                    ratio,
                    kneeDb,
                    attackMs,
                    releaseMs,
                    autoRelease);
            case VARIMU:
                updateVariableMuDetector(
                    state,
                    std::abs(detectorSample) * state.lastGain,
                    thresholdDb,
                    ratio,
                    kneeDb,
                    attackMs,
                    releaseMs,
                    activeVarMuTimeConstant);
                break;
        }

        if ((mode == FET || mode == VARIMU)
            && state.discreteFadeRemaining > 0)
        {
            const float position = 1.0f
                - static_cast<float>(state.discreteFadeRemaining - 1)
                    / static_cast<float>(discreteControlCrossfadeSamples());
            state.controlDb = state.discreteFadeStartDb
                + position * (state.controlDb - state.discreteFadeStartDb);
            --state.discreteFadeRemaining;
            state.controlDb = juce::jlimit(-60.0f, 0.0f, state.controlDb);
            state.lastGain = juce::Decibels::decibelsToGain(state.controlDb);
        }
        return state.controlDb;
    }

    void fillGainEnvelope(
        const juce::AudioBuffer<float>& detector,
        Mode mode,
        const Controls& controls,
        std::array<float, 2>* channelGrDb,
        bool advanceTransitions) noexcept
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        const int channels = detector.getNumChannels();
        std::array<double, 2> channelGrSum {};
        std::array<int, 2> channelGrCount {};

        for (int sample = 0; sample < detector.getNumSamples(); ++sample)
        {
            const float thresholdDb = controlScratch.getSample(
                thresholdControl, sample);
            const float ratio = controlScratch.getSample(ratioControl, sample);
            const float kneeDb = controlScratch.getSample(kneeControl, sample);
            const float attackMs = controlScratch.getSample(attackControl, sample);
            const float releaseMs = controlScratch.getSample(releaseControl, sample);

            std::array<float, 2> unlinkedGr {};
            float commonDetector = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const float detectorSample = detector.getSample(channel, sample);
                auto& channelState = detectorStates[modeIndex][
                    static_cast<size_t>(juce::jmin(channel, 1))];
                commonDetector = juce::jmax(
                    commonDetector, std::abs(detectorSample));
                unlinkedGr[static_cast<size_t>(juce::jmin(channel, 1))]
                    = updateDetector(
                        mode,
                        channelState,
                        detectorSample,
                        thresholdDb,
                        ratio,
                        kneeDb,
                        attackMs,
                        releaseMs,
                        controls.autoRelease);
            }
            if (channels == 1)
                unlinkedGr[1] = unlinkedGr[0];

            float linkedGr = 0.0f;
            linkedGr = updateDetector(
                mode,
                linkedDetectorStates[modeIndex],
                commonDetector,
                thresholdDb,
                ratio,
                kneeDb,
                attackMs,
                releaseMs,
                controls.autoRelease);

            float linkTransition = 1.0f;
            if (advanceTransitions && stereoLinkFadeRemaining > 0)
            {
                linkTransition = 1.0f
                    - static_cast<float>(stereoLinkFadeRemaining - 1)
                        / static_cast<float>(stereoLinkCrossfadeSamples());
            }

            float photoResetTransition = 1.0f;
            if (mode == OPTO && advanceTransitions && photoResetFadeRemaining > 0)
            {
                photoResetTransition = 1.0f
                    - static_cast<float>(photoResetFadeRemaining - 1)
                        / static_cast<float>(qualityCrossfadeSamples());
            }

            for (int channel = 0; channel < channels; ++channel)
            {
                const size_t stateChannel = static_cast<size_t>(
                    juce::jmin(channel, 1));
                float targetGr = controls.stereoChain
                    ? linkedGr : unlinkedGr[stateChannel];
                if (advanceTransitions && stereoLinkFadeRemaining > 0)
                {
                    targetGr = stereoLinkFadeStartGrDb[stateChannel]
                        + linkTransition
                            * (targetGr - stereoLinkFadeStartGrDb[stateChannel]);
                }
                if (mode == OPTO && advanceTransitions && photoResetFadeRemaining > 0)
                {
                    targetGr = photoResetFadeStartGrDb[stateChannel]
                        + photoResetTransition
                            * (targetGr - photoResetFadeStartGrDb[stateChannel]);
                }

                targetGr = juce::jlimit(-60.0f, 0.0f, targetGr);
                const float linearGain = juce::jlimit(
                    0.001f, 1.0f,
                    juce::Decibels::decibelsToGain(targetGr));
                gainScratch.setSample(channel, sample, linearGain);
                lastDetectorGrDbByMode[modeIndex][stateChannel] = targetGr;
                channelGrSum[stateChannel] += targetGr;
                ++channelGrCount[stateChannel];
            }

            if (advanceTransitions && stereoLinkFadeRemaining > 0)
                --stereoLinkFadeRemaining;
            if (mode == OPTO && advanceTransitions && photoResetFadeRemaining > 0)
                --photoResetFadeRemaining;
        }

        if (channelGrDb != nullptr)
        {
            for (size_t channel = 0; channel < channelGrDb->size(); ++channel)
            {
                if (channelGrCount[channel] > 0)
                {
                    (*channelGrDb)[channel] = static_cast<float>(
                        channelGrSum[channel]
                        / static_cast<double>(channelGrCount[channel]));
                }
                else
                {
                    (*channelGrDb)[channel] = channel > 0
                        ? (*channelGrDb)[0] : 0.0f;
                }
            }
        }
    }

    float gainControlAt(
        Mode mode,
        int channel,
        const float* currentValues,
        int currentSamples,
        double position) const noexcept
    {
        const auto& history = nonlinearControlHistory[
            static_cast<size_t>(mode)];
        const int lowerIndex = static_cast<int>(std::floor(position));
        const int upperIndex = lowerIndex + 1;
        const float fraction = static_cast<float>(
            position - static_cast<double>(lowerIndex));
        const auto valueAt = [&](int index) noexcept
        {
            if (index >= 0)
                return currentValues[
                    juce::jmin(currentSamples - 1, index)];
            return history.gainBeforeBlock(channel, index);
        };

        const float lower = valueAt(lowerIndex);
        const float upper = valueAt(upperIndex);
        const float interpolated = lower + fraction * (upper - lower);
        return juce::jlimit(
            juce::jmin(lower, upper),
            juce::jmax(lower, upper),
            interpolated);
    }

    float driveControlAt(
        Mode mode,
        const float* currentValues,
        int currentSamples,
        double position) const noexcept
    {
        const auto& history = nonlinearControlHistory[
            static_cast<size_t>(mode)];
        const int lowerIndex = static_cast<int>(std::floor(position));
        const int upperIndex = lowerIndex + 1;
        const float fraction = static_cast<float>(
            position - static_cast<double>(lowerIndex));
        const auto valueAt = [&](int index) noexcept
        {
            if (index >= 0)
                return currentValues[
                    juce::jmin(currentSamples - 1, index)];
            return history.driveBeforeBlock(index);
        };

        const float lower = valueAt(lowerIndex);
        const float upper = valueAt(upperIndex);
        const float interpolated = lower + fraction * (upper - lower);
        return juce::jlimit(
            juce::jmin(lower, upper),
            juce::jmax(lower, upper),
            interpolated);
    }

    float processOutputTransformer(
        int quality,
        Mode mode,
        int channel,
        float sample,
        float driveDb) noexcept
    {
        auto& transformers = qualityOutputTransformers[
            static_cast<size_t>(quality)][static_cast<size_t>(mode)];
        if (transformers.empty())
            return sample;

        const float drivePosition = juce::jlimit(
            0.0f, 1.0f, driveDb / 18.0f);
        TransformerModel::Params params;
        params.driveDb = -9.0f + driveDb * 0.72f;
        params.saturation = 0.01f + drivePosition * 0.16f;
        params.enableLfBoost = false;
        params.enableHfRolloff = false;
        params.dcBias = 0.0f;
        params.temperature = 40.0f;

        const float architectureAmount = mode == FET ? 0.045f
            : mode == OPTO ? 0.030f
            : mode == VCA ? 0.008f : 0.050f;
        const float qualityRateScale = quality == nativeQuality ? 1.0f
            : quality == quality2x ? 0.5f : 0.25f;
        const float transformerMix = drivePosition
            * architectureAmount * qualityRateScale;
        auto& transformer = transformers[static_cast<size_t>(
            juce::jlimit(
                0, static_cast<int>(transformers.size()) - 1, channel))];
        const float transformerLinear = 1.0f + params.saturation * 2.0f;
        const float transformerTrim = 1.0f
            / (transformerLinear * transformerLinear);
        const float coloured = transformer.process(
            0, sample, params) * transformerTrim;
        return sample + (coloured - sample) * transformerMix;
    }

    static float continuousVariableMuPushPull(float sample) noexcept
    {
        constexpr float linearLimit = 0.20f;
        constexpr float saturationLimit = 0.50f;
        const float magnitude = std::abs(sample);
        if (magnitude <= linearLimit)
            return sample;

        const float saturated = std::tanh(sample * 1.2f) / 1.2f;
        if (magnitude >= saturationLimit)
            return saturated;

        const float position = (magnitude - linearLimit)
            / (saturationLimit - linearLimit);
        const float smoothPosition = position * position * position
            * (position * (position * 6.0f - 15.0f) + 10.0f);
        return sample + (saturated - sample) * smoothPosition;
    }

    float processGainCell(
        int quality,
        Mode mode,
        int channel,
        float sample,
        float linearGain,
        float driveDb) noexcept
    {
        const float safeGain = juce::jlimit(0.001f, 1.0f, linearGain);
        const float gainReductionDb = -juce::Decibels::gainToDecibels(
            safeGain, -60.0f);
        const float depth = juce::jlimit(0.0f, 1.0f, gainReductionDb / 24.0f);
        const float drivePosition = juce::jlimit(0.0f, 1.0f, driveDb / 18.0f);
        auto& engine = gainEngines[static_cast<size_t>(quality)][
            static_cast<size_t>(mode)];

        switch (mode)
        {
            case FET:
            {
                const double driveAmount = juce::jlimit(
                    0.0, 1.0,
                    static_cast<double>(drivePosition + depth * 0.35f));
                const double driveGain = juce::Decibels::decibelsToGain(
                    driveDb * 0.72f);
                const double inputAmplifier = std::tanh(
                    static_cast<double>(sample) * driveGain
                    * (1.0 + 0.08 * depth));
                const double normalised = inputAmplifier / juce::jmax(1.0, driveGain);
                const float output = static_cast<float>(
                    fetGainCell.vcaNonlinearity(
                        normalised * safeGain,
                        safeGain,
                        driveAmount));
                return processOutputTransformer(
                    quality, mode, channel, output, driveDb);
            }
            case OPTO:
            {
                PhotoCompressor::Params params;
                params.outputGain = 0.5f;
                params.mix = 1.0f;
                params.mode = 0;
                const float cleanLdrOutput = sample * safeGain;
                const float driveGain = juce::Decibels::decibelsToGain(
                    driveDb * 0.82f);
                const float output = engine.photo.processWithGain(
                    channel,
                    cleanLdrOutput * driveGain,
                    1.0f,
                    params)
                    / juce::jmax(1.0f, driveGain);
                return processOutputTransformer(
                    quality, mode, channel, output, driveDb);
            }
            case VCA:
            {
                VcaCompressor::Params params;
                params.outputGain = 0.5f;
                params.mix = 1.0f;
                float output = engine.vca.processWithGain(
                    channel, sample, safeGain, params);
                const float colourAmount = juce::jlimit(
                    0.0f, 0.035f,
                    drivePosition * 0.025f + depth * 0.006f);
                const float driven = output
                    * juce::Decibels::decibelsToGain(driveDb * 0.28f);
                const float saturated = std::tanh(driven)
                    / juce::jmax(
                        1.0f,
                        juce::Decibels::decibelsToGain(driveDb * 0.28f));
                output += (saturated - output) * colourAmount;
                return processOutputTransformer(
                    quality, mode, channel, output, driveDb);
            }
            case VARIMU:
            {
                const float coupledDriveDb = juce::jlimit(
                    0.0f, 18.0f,
                    driveDb * 0.68f + gainReductionDb * 0.16f);
                const float driveGain = juce::Decibels::decibelsToGain(
                    coupledDriveDb);
                const float driven = sample * driveGain;
                const float transformerAmount = 0.012f
                    * (drivePosition + depth * 0.5f);
                const float inputTransformer = driven
                    + transformerAmount * driven * std::abs(driven);
                const float balanced = continuousVariableMuPushPull(
                    inputTransformer * safeGain)
                    / juce::jmax(1.0f, driveGain);
                const float mismatch = juce::jlimit(
                    0.0f, 0.045f,
                    drivePosition * 0.025f + depth * 0.015f);
                const float asymmetric = TubeTriode::transferFunction(
                    balanced * (1.0f + depth * 0.25f));
                const float output = balanced
                    + (asymmetric - balanced) * mismatch;
                return processOutputTransformer(
                    quality, mode, channel, output, driveDb);
            }
        }
        return sample;
    }

    void processAudioQuality(
        juce::AudioBuffer<float>& buffer,
        Mode mode,
        int quality) noexcept
    {
        const size_t modeIndex = static_cast<size_t>(mode);
        if (quality == nativeQuality)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* audio = buffer.getWritePointer(channel);
                const auto* gain = gainScratch.getReadPointer(channel);
                const auto* drive = controlScratch.getReadPointer(driveControl);
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    audio[sample] = processGainCell(
                        quality, mode, channel,
                        audio[sample], gain[sample], drive[sample]);
                }
            }
            nativeModeDelays[modeIndex].process(buffer);
            return;
        }

        auto& oversampler = quality == quality4x
            ? nonlinearOversamplers4x[modeIndex]
            : nonlinearOversamplers2x[modeIndex];
        auto& nonlinearBuffer = nonlinearBuffers[modeIndex];
        const int factor = oversampler.getFactor();
        const double upsamplingDelayInBaseSamples = 0.5
            * static_cast<double>(oversampler.getLatency() + 1);

        for (int offset = 0; offset < buffer.getNumSamples();
             offset += nonlinearMaxBlock)
        {
            const int blockSamples = juce::jmin(
                nonlinearMaxBlock,
                buffer.getNumSamples() - offset);
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* destination = nonlinearBuffer.getWritePointer(channel);
                if (channel < buffer.getNumChannels())
                {
                    std::copy_n(
                        buffer.getReadPointer(channel) + offset,
                        blockSamples,
                        destination);
                }
                else
                {
                    std::fill_n(destination, blockSamples, 0.0f);
                }
            }

            oversampler.process(
                nonlinearBuffer,
                blockSamples,
                [&](auto& oversampled, int oversampledSamples) noexcept
                {
                    for (int channel = 0;
                         channel < buffer.getNumChannels();
                         ++channel)
                    {
                        auto* audio = oversampled.getWritePointer(channel);
                        const auto* gain = gainScratch.getReadPointer(channel);
                        const auto* drive = controlScratch.getReadPointer(
                            driveControl);
                        for (int sample = 0; sample < oversampledSamples; ++sample)
                        {
                            const double controlPosition =
                                static_cast<double>(offset)
                                + static_cast<double>(sample)
                                    / static_cast<double>(factor)
                                - upsamplingDelayInBaseSamples;
                            const float interpolatedGain = gainControlAt(
                                mode, channel, gain,
                                buffer.getNumSamples(), controlPosition);
                            const float interpolatedDrive = driveControlAt(
                                mode, drive,
                                buffer.getNumSamples(), controlPosition);
                            audio[sample] = processGainCell(
                                quality, mode, channel,
                                audio[sample],
                                juce::jlimit(
                                    0.001f, 1.0f, interpolatedGain),
                                juce::jlimit(
                                    0.0f, 18.0f, interpolatedDrive));
                        }
                    }
                });

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                std::copy_n(
                    nonlinearBuffer.getReadPointer(channel),
                    blockSamples,
                    buffer.getWritePointer(channel) + offset);
            }
        }

        if (quality == quality4x)
            oversampling4xAlignmentDelays[modeIndex].process(buffer);
        else
            oversampling2xAlignmentDelays[modeIndex].process(buffer);
    }

    float processMode(
        juce::AudioBuffer<float>& buffer,
        const juce::AudioBuffer<float>& detector,
        Mode mode,
        const Controls& controls,
        std::array<float, 2>* channelGrDb,
        bool advanceTransitions)
    {
        fillGainEnvelope(
            detector, mode, controls, channelGrDb, advanceTransitions);

        auto& controlHistory = nonlinearControlHistory[
            static_cast<size_t>(mode)];
        const auto* driveValues = controlScratch.getReadPointer(driveControl);
        const int rightChannel = juce::jmin(
            1, buffer.getNumChannels() - 1);
        controlHistory.prime(
            gainScratch.getSample(0, 0),
            gainScratch.getSample(rightChannel, 0),
            driveValues[0]);

        if (advanceTransitions && qualityFadeRemaining > 0)
        {
            const int fadeSamples = juce::jmin(
                buffer.getNumSamples(), qualityFadeRemaining);
            copyBuffer(
                qualityOldWet, buffer,
                buffer.getNumChannels(), buffer.getNumSamples());
            juce::AudioBuffer<float> oldQualityBlock(
                qualityOldWet.getArrayOfWritePointers(),
                buffer.getNumChannels(),
                buffer.getNumSamples());

            processAudioQuality(buffer, mode, activeQuality);
            processAudioQuality(oldQualityBlock, mode, oldQuality);
            const int totalFade = qualityCrossfadeSamples();
            for (int sample = 0; sample < fadeSamples; ++sample)
            {
                const float position = 1.0f
                    - static_cast<float>(qualityFadeRemaining)
                        / static_cast<float>(totalFade);
                const float newMix = juce::jlimit(
                    0.0f, 1.0f, position);
                const float oldMix = 1.0f - newMix;
                for (int channel = 0;
                     channel < buffer.getNumChannels();
                     ++channel)
                {
                    buffer.setSample(
                        channel,
                        sample,
                        oldQualityBlock.getSample(channel, sample) * oldMix
                            + buffer.getSample(channel, sample) * newMix);
                }
                --qualityFadeRemaining;
            }
            if (fadeSamples < buffer.getNumSamples())
            {
                // The new path has already processed the complete block.
                qualityFadeRemaining = 0;
            }
        }
        else
        {
            processAudioQuality(buffer, mode, activeQuality);
        }

        controlHistory.append(
            gainScratch,
            driveValues,
            buffer.getNumChannels(),
            buffer.getNumSamples());

        double grSum = 0.0;
        int grCount = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* gain = gainScratch.getReadPointer(channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                grSum += juce::Decibels::gainToDecibels(
                    juce::jlimit(0.001f, 1.0f, gain[sample]), -60.0f);
                ++grCount;
            }
        }
        return grCount > 0
            ? static_cast<float>(grSum / static_cast<double>(grCount))
            : 0.0f;
    }

    void processChunk(
        juce::AudioBuffer<float>& buffer,
        const Controls& controls)
    {
        const int samples = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        jassert(samples <= allocatedSamples);

        for (auto* scratch : {
                 &inputReference, &detectorBuffer, &modeInput, &oldWet,
                 &qualityOldWet, &lowBand, &highLowPassed, &gainScratch
             })
        {
            scratch->setSize(channels, samples, false, false, true);
        }
        controlScratch.setSize(controlCount, samples, false, false, true);
        fillControlScratch(controls, samples);
        updateDiscreteSelections(controls);

        if (photoResetRequested.exchange(false, std::memory_order_acq_rel))
        {
            photoResetFadeStartGrDb = lastDetectorGrDbByMode[
                static_cast<size_t>(OPTO)];
            detectorStates[static_cast<size_t>(OPTO)][0].reset();
            detectorStates[static_cast<size_t>(OPTO)][1].reset();
            linkedDetectorStates[static_cast<size_t>(OPTO)].reset();
            for (auto& quality : gainEngines)
                quality[static_cast<size_t>(OPTO)].photo.reset();
            photoResetFadeRemaining = currentMode == OPTO
                ? qualityCrossfadeSamples() : 0;
        }

        if (!stereoChainStateInitialised)
        {
            previousStereoChain = controls.stereoChain;
            stereoChainStateInitialised = true;
        }
        else if (controls.stereoChain != previousStereoChain)
        {
            stereoLinkFadeStartGrDb = lastDetectorGrDbByMode[
                static_cast<size_t>(currentMode)];
            stereoLinkFadeRemaining = stereoLinkCrossfadeSamples();
            previousStereoChain = controls.stereoChain;
        }

        requestedMode = controls.mode;
        requestedQuality = qualityForControls(controls);

        // Serialise discrete transitions so no path participates in two
        // unrelated crossfades at the same time.
        if (requestedMode != currentMode
            && xfadeRemaining == 0
            && qualityFadeRemaining == 0)
        {
            oldMode = currentMode;
            currentMode = requestedMode;
            resetMode(currentMode);
            transitionTrimDb = 0.0f;
            stereoLinkFadeRemaining = 0;
            xfadeRemaining = modeCrossfadeSamples();
        }
        else if (requestedQuality != activeQuality
                 && xfadeRemaining == 0
                 && qualityFadeRemaining == 0)
        {
            oldQuality = activeQuality;
            activeQuality = requestedQuality;
            resetQualityPath(currentMode, activeQuality);
            qualityFadeRemaining = qualityCrossfadeSamples();
        }

        const float requestedLowSplitHz = juce::jlimit(
            30.0f, 600.0f, controls.lowBleedHz);
        if (requestedLowSplitHz != lowSplitCutoffHz)
        {
            lowSplitCutoffHz = requestedLowSplitHz;
            lowSplit.setCutoffFrequency(lowSplitCutoffHz);
        }
        const float requestedHighSplitHz = juce::jlimit(
            1000.0f, 18000.0f, controls.highBleedHz);
        if (requestedHighSplitHz != highSplitCutoffHz)
        {
            highSplitCutoffHz = requestedHighSplitHz;
            highSplit.setCutoffFrequency(highSplitCutoffHz);
        }

        copyBuffer(inputReference, buffer, channels, samples);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* input = inputReference.getWritePointer(channel);
            const auto* inputDb = controlScratch.getReadPointer(inputControl);
            for (int sample = 0; sample < samples; ++sample)
            {
                input[sample] *= juce::Decibels::decibelsToGain(
                    inputDb[sample]);
            }
        }

        copyBuffer(detectorBuffer, inputReference, channels, samples);
        if (controls.sidechainHpf)
        {
            juce::dsp::AudioBlock<float> sidechainBlock(detectorBuffer);
            sidechainBlock = sidechainBlock.getSubBlock(
                0, static_cast<size_t>(samples));
            juce::dsp::ProcessContextReplacing<float> context(sidechainBlock);
            sidechainHigh.process(context);
        }

        copyBuffer(buffer, inputReference, channels, samples);
        copyBuffer(modeInput, inputReference, channels, samples);
        displayThresholdDb.store(
            controlScratch.getSample(thresholdControl, samples - 1),
            std::memory_order_relaxed);

        std::array<float, 2> newGrByChannel {};
        const float newGr = processMode(
            buffer,
            detectorBuffer,
            currentMode,
            controls,
            &newGrByChannel,
            true);

        if (xfadeRemaining > 0)
        {
            const int oldModeSamples = juce::jmin(samples, xfadeRemaining);
            copyBuffer(oldWet, modeInput, channels, oldModeSamples);
            juce::AudioBuffer<float> oldWetBlock(
                oldWet.getArrayOfWritePointers(),
                channels,
                oldModeSamples);
            juce::AudioBuffer<float> oldDetectorBlock(
                detectorBuffer.getArrayOfWritePointers(),
                channels,
                oldModeSamples);
            processMode(
                oldWetBlock,
                oldDetectorBlock,
                oldMode,
                controls,
                nullptr,
                false);

            const float oldDb = rmsDb(
                bufferEnergy(oldWetBlock, channels, oldModeSamples),
                oldModeSamples * channels);
            const float newDb = rmsDb(
                bufferEnergy(buffer, channels, oldModeSamples),
                oldModeSamples * channels);
            if (oldDb > -80.0f && newDb > -80.0f)
            {
                const float difference = juce::jlimit(
                    -9.0f, 9.0f, oldDb - newDb);
                constexpr double trackingSeconds = 0.128;
                const float alpha = 1.0f - std::exp(
                    -static_cast<float>(oldModeSamples)
                    / static_cast<float>(sampleRate * trackingSeconds));
                transitionTrimDb += alpha
                    * (difference - transitionTrimDb);
            }

            const int totalFade = modeCrossfadeSamples();
            for (int sample = 0; sample < oldModeSamples; ++sample)
            {
                const float position = 1.0f
                    - static_cast<float>(xfadeRemaining)
                        / static_cast<float>(totalFade);
                const float oldMix = std::sqrt(
                    juce::jlimit(0.0f, 1.0f, 1.0f - position));
                const float newMix = std::sqrt(
                    juce::jlimit(0.0f, 1.0f, position));
                const float align = juce::Decibels::decibelsToGain(
                    transitionTrimDb * (1.0f - position));
                for (int channel = 0; channel < channels; ++channel)
                {
                    buffer.setSample(
                        channel,
                        sample,
                        oldWet.getSample(channel, sample) * oldMix
                            + buffer.getSample(channel, sample)
                                * align * newMix);
                }
                --xfadeRemaining;
            }
            if (xfadeRemaining == 0)
                transitionTrimDb = 0.0f;
        }

        inputPathDelay.process(inputReference);

        copyBuffer(lowBand, inputReference, channels, samples);
        copyBuffer(highLowPassed, inputReference, channels, samples);
        juce::dsp::AudioBlock<float> lowBlock(lowBand);
        juce::dsp::AudioBlock<float> highBlock(highLowPassed);
        lowBlock = lowBlock.getSubBlock(0, static_cast<size_t>(samples));
        highBlock = highBlock.getSubBlock(0, static_cast<size_t>(samples));
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock);
        juce::dsp::ProcessContextReplacing<float> highContext(highBlock);
        lowSplit.process(lowContext);
        highSplit.process(highContext);

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* wet = buffer.getWritePointer(channel);
            const auto* low = lowBand.getReadPointer(channel);
            const auto* highLow = highLowPassed.getReadPointer(channel);
            const auto* dry = inputReference.getReadPointer(channel);
            for (int sample = 0; sample < samples; ++sample)
            {
                const float high = dry[sample] - highLow[sample];
                wet[sample] += low[sample] * controls.lowBleedAmt
                             + high * controls.highBleedAmt;
            }
        }

        const double inputEnergy = bufferEnergy(
            inputReference, channels, samples);
        const double wetEnergy = bufferEnergy(buffer, channels, samples);
        const float inputDbNow = rmsDb(
            inputEnergy, samples * channels);
        const float wetDbNow = rmsDb(
            wetEnergy, samples * channels);
        displayGraphGainDb.store(
            juce::jlimit(-48.0f, 24.0f, wetDbNow - inputDbNow),
            std::memory_order_relaxed);

        if (controls.autoLevel)
        {
            const float desired = juce::jlimit(
                -12.0f, 12.0f, inputDbNow - wetDbNow);
            const float timeMs = desired > autoGainDb ? 250.0f : 900.0f;
            const float alpha = 1.0f - std::exp(
                -static_cast<float>(samples)
                / static_cast<float>(sampleRate * timeMs * 0.001));
            autoGainDb += alpha * (desired - autoGainDb);
        }
        else
        {
            const float alpha = 1.0f - std::exp(
                -static_cast<float>(samples)
                / static_cast<float>(sampleRate * 0.25));
            autoGainDb += alpha * (0.0f - autoGainDb);
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* wet = buffer.getWritePointer(channel);
            const auto* dry = inputReference.getReadPointer(channel);

            for (int sample = 0; sample < samples; ++sample)
            {
                const float makeupGain = juce::Decibels::decibelsToGain(
                    controlScratch.getSample(makeupControl, sample));
                const float outputGain = juce::Decibels::decibelsToGain(
                    controlScratch.getSample(outputControl, sample));
                const float mix = controlScratch.getSample(mixControl, sample);
                const float wetGain = makeupGain
                    * juce::Decibels::decibelsToGain(autoGainDb);
                wet[sample] = (
                    wet[sample] * wetGain * mix
                    + dry[sample] * (1.0f - mix)) * outputGain;
            }
        }

        const float blockSeconds = static_cast<float>(
            samples / sampleRate);
        instantaneousGrDb = newGr;
        const float meterAlpha = 1.0f - std::exp(
            -blockSeconds / 0.08f);
        smoothedGrDb += meterAlpha * (newGr - smoothedGrDb);
        for (size_t channel = 0;
             channel < smoothedGrDbByChannel.size();
             ++channel)
        {
            const float target = static_cast<int>(channel) < channels
                ? newGrByChannel[channel] : newGrByChannel[0];
            smoothedGrDbByChannel[channel] += meterAlpha
                * (target - smoothedGrDbByChannel[channel]);
        }
    }

    std::array<std::array<EngineSet, 4>, qualityCount> gainEngines;
    JFET_Primitive fetGainCell {
        JFET_Primitive::N2N5457(), 701
    };
    std::array<std::array<std::vector<TransformerModel>, 4>, qualityCount>
        qualityOutputTransformers;

    std::array<std::array<DetectorState, 2>, 4> detectorStates;
    std::array<DetectorState, 4> linkedDetectorStates;

    juce::AudioBuffer<float> inputReference;
    juce::AudioBuffer<float> detectorBuffer;
    juce::AudioBuffer<float> modeInput;
    juce::AudioBuffer<float> oldWet;
    juce::AudioBuffer<float> qualityOldWet;
    juce::AudioBuffer<float> lowBand;
    juce::AudioBuffer<float> highLowPassed;
    juce::AudioBuffer<float> gainScratch;
    juce::AudioBuffer<float> controlScratch;

    std::array<patina::Oversampler<float>, 4> nonlinearOversamplers2x;
    std::array<patina::Oversampler<float>, 4> nonlinearOversamplers4x;
    std::array<patina::compat::AudioBuffer<float>, 4> nonlinearBuffers;
    std::array<FixedDelay, 4> oversampling2xAlignmentDelays;
    std::array<FixedDelay, 4> oversampling4xAlignmentDelays;
    std::array<FixedDelay, 4> nativeModeDelays;
    FixedDelay inputPathDelay;
    std::array<ControlHistory, 4> nonlinearControlHistory;

    juce::dsp::StateVariableTPTFilter<float> lowSplit;
    juce::dsp::StateVariableTPTFilter<float> highSplit;
    juce::dsp::StateVariableTPTFilter<float> sidechainHigh;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               controlCount> controlSmoothers;

    float lowSplitCutoffHz = 120.0f;
    float highSplitCutoffHz = 8000.0f;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int allocatedSamples = 1;
    int nonlinearMaxBlock = 1;
    int nonlinearLatencySamples = 0;

    Mode currentMode = FET;
    Mode requestedMode = FET;
    Mode oldMode = FET;
    int xfadeRemaining = 0;
    int activeQuality = nativeQuality;
    int requestedQuality = nativeQuality;
    int oldQuality = nativeQuality;
    int qualityFadeRemaining = 0;

    float smoothedGrDb = 0.0f;
    std::array<float, 2> smoothedGrDbByChannel {};
    float instantaneousGrDb = 0.0f;
    float autoGainDb = 0.0f;
    bool previousStereoChain = false;
    bool stereoChainStateInitialised = false;
    int stereoLinkFadeRemaining = 0;
    std::array<float, 2> stereoLinkFadeStartGrDb {};
    std::array<std::array<float, 2>, 4> lastDetectorGrDbByMode {};
    float transitionTrimDb = 0.0f;
    int photoResetFadeRemaining = 0;
    std::array<float, 2> photoResetFadeStartGrDb {};
    bool controlsInitialised = false;
    bool discreteSelectionsInitialised = false;
    int activeFetRatioIndex = 0;
    int activeVarMuTimeConstant = 1;

    std::atomic<bool> photoResetRequested { false };
    std::atomic<float> displayThresholdDb { -18.0f };
    std::atomic<float> displayGraphGainDb { 0.0f };
};
