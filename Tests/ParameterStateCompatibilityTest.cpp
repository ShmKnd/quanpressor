#include "../Source/PluginProcessor.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
struct ExpectedParameter
{
    const char* id;
    float minimum;
    float maximum;
    float defaultValue;
};

constexpr std::array<ExpectedParameter, 26> expectedParameters {{
    { "mode", 0.0f, 3.0f, 0.0f },
    { "input", -24.0f, 24.0f, 0.0f },
    { "threshold", -48.0f, 0.0f, -18.0f },
    { "ratio", 1.5f, 20.0f, 4.0f },
    { "drive", 0.0f, 18.0f, 0.0f },
    { "attack", 0.02f, 100.0f, 10.0f },
    { "release", 20.0f, 1200.0f, 150.0f },
    { "knee", 0.0f, 18.0f, 6.0f },
    { "makeup", -12.0f, 24.0f, 0.0f },
    { "highFreq", 1000.0f, 18000.0f, 8000.0f },
    { "highAmt", 0.0f, 1.0f, 0.0f },
    { "lowFreq", 30.0f, 600.0f, 120.0f },
    { "lowAmt", 0.0f, 1.0f, 0.0f },
    { "output", -24.0f, 24.0f, 0.0f },
    { "mix", 0.0f, 100.0f, 100.0f },
    { "muSpeed", 0.0f, 5.0f, 2.0f },
    { "vuMode", 0.0f, 2.0f, 0.0f },
    { "autoLevel", 0.0f, 1.0f, 0.0f },
    { "autoRelease", 0.0f, 1.0f, 0.0f },
    { "grMatch", 0.0f, 1.0f, 0.0f },
    { "noMeter", 0.0f, 1.0f, 0.0f },
    { "externalSidechain", 0.0f, 1.0f, 0.0f },
    { "sidechainHpf", 0.0f, 1.0f, 0.0f },
    { "stereoChain", 0.0f, 1.0f, 1.0f },
    { "oversampling", 0.0f, 1.0f, 0.0f },
    { "oversamplingFactor", 0.0f, 1.0f, 0.0f }
}};

bool nearlyEqual(float a, float b)
{
    return std::abs(a - b) <= 0.051f;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceGui;
    QuanpressorAudioProcessor source;

    if (source.getParameters().size()
        != static_cast<int>(expectedParameters.size()))
    {
        std::cerr << "Parameter count changed: "
                  << source.getParameters().size() << '\n';
        return 1;
    }

    for (const auto& expected : expectedParameters)
    {
        auto* parameter = source.apvts.getParameter(expected.id);
        if (parameter == nullptr)
        {
            std::cerr << "Missing parameter: " << expected.id << '\n';
            return 1;
        }
        const float minimum = parameter->convertFrom0to1(0.0f);
        const float maximum = parameter->convertFrom0to1(1.0f);
        const float defaultValue = parameter->convertFrom0to1(
            parameter->getDefaultValue());
        if (!nearlyEqual(minimum, expected.minimum)
            || !nearlyEqual(maximum, expected.maximum)
            || !nearlyEqual(defaultValue, expected.defaultValue))
        {
            std::cerr << "Range or default changed for " << expected.id
                      << ": " << minimum << ", " << maximum << ", "
                      << defaultValue << '\n';
            return 1;
        }
    }

    if (source.getBusCount(true) != 2
        || source.getChannelCountOfBus(true, 0) != 2
        || source.getChannelCountOfBus(true, 1) != 2)
    {
        std::cerr << "Default main and sidechain bus configuration is invalid\n";
        return 1;
    }

    auto sidechainLayout = source.getBusesLayout();
    sidechainLayout.inputBuses.set(
        1, juce::AudioChannelSet::mono());
    if (!source.isBusesLayoutSupported(sidechainLayout))
    {
        std::cerr << "Mono external sidechain layout was rejected\n";
        return 1;
    }
    sidechainLayout.inputBuses.set(
        1, juce::AudioChannelSet::stereo());
    if (!source.isBusesLayoutSupported(sidechainLayout))
    {
        std::cerr << "Stereo external sidechain layout was rejected\n";
        return 1;
    }

    sidechainLayout.inputBuses.set(
        1, juce::AudioChannelSet::mono());
    if (!source.setBusesLayout(sidechainLayout))
    {
        std::cerr << "Mono external sidechain layout could not be enabled\n";
        return 1;
    }
    source.prepareToPlay(48000.0, 64);
    const auto setParameter = [&source](const char* parameterId, float value)
    {
        if (auto* parameter = source.apvts.getParameter(parameterId))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(value));
    };
    setParameter("mode", 2.0f);
    setParameter("threshold", -24.0f);
    setParameter("ratio", 8.0f);
    setParameter("attack", 1.0f);
    setParameter("release", 250.0f);
    setParameter("knee", 3.0f);
    setParameter("oversampling", 0.0f);

    juce::AudioBuffer<float> sidechainProcessBuffer(3, 64);
    juce::MidiBuffer midi;
    for (int blockIndex = 0; blockIndex < 900; ++blockIndex)
    {
        for (int sample = 0; sample < 64; ++sample)
        {
            sidechainProcessBuffer.setSample(0, sample, 0.8f);
            sidechainProcessBuffer.setSample(1, sample, 0.8f);
            sidechainProcessBuffer.setSample(2, sample, 0.0f);
        }
        source.processBlock(sidechainProcessBuffer, midi);
    }
    if (source.meters.grDb.load() > -10.0f)
    {
        std::cerr << "Main input did not drive the detector while EXT SC was off\n";
        return 1;
    }
    if (juce::jmax(sidechainProcessBuffer.getMagnitude(0, 0, 64),
                   sidechainProcessBuffer.getMagnitude(1, 0, 64)) < 1.0e-5f)
    {
        std::cerr << "Main insert audio was lost while EXT SC was off\n";
        return 1;
    }

    setParameter("externalSidechain", 1.0f);
    for (int blockIndex = 0; blockIndex < 900; ++blockIndex)
    {
        for (int sample = 0; sample < 64; ++sample)
        {
            sidechainProcessBuffer.setSample(0, sample, 0.02f);
            sidechainProcessBuffer.setSample(1, sample, 0.02f);
            sidechainProcessBuffer.setSample(2, sample, 0.8f);
        }
        source.processBlock(sidechainProcessBuffer, midi);
    }
    if (source.meters.grDb.load() > -10.0f)
    {
        std::cerr << "Processor did not route the external sidechain bus to the detector\n";
        return 1;
    }
    if (source.meters.graphInputDb.load() < -3.0f)
    {
        std::cerr << "Graph input did not follow the external detector source\n";
        return 1;
    }
    if (juce::jmax(sidechainProcessBuffer.getMagnitude(0, 0, 64),
                   sidechainProcessBuffer.getMagnitude(1, 0, 64)) < 1.0e-5f)
    {
        std::cerr << "Main insert audio was lost while EXT SC was on\n";
        return 1;
    }

    if (source.getNumPrograms() != quanpressorFactoryPresetCount)
    {
        std::cerr << "Factory preset count does not match the host program list\n";
        return 1;
    }
    for (int presetIndex = 0;
         presetIndex < quanpressorFactoryPresetCount;
         ++presetIndex)
    {
        source.loadFactoryPreset(presetIndex);
        if (source.getCurrentProgram() != presetIndex
            || source.getCurrentPresetName()
                != quanpressorFactoryPresets[presetIndex].name
            || source.getProgramName(presetIndex)
                != quanpressorFactoryPresets[presetIndex].name)
        {
            std::cerr << "Factory preset metadata failed at index "
                      << presetIndex << '\n';
            return 1;
        }

        const auto& preset = quanpressorFactoryPresets[presetIndex];
        for (int valueIndex = 0;
             valueIndex < preset.valueCount;
             ++valueIndex)
        {
            const auto& expected = preset.values[valueIndex];
            const auto* value = source.apvts.getRawParameterValue(
                expected.parameterId);
            if (value == nullptr
                || !nearlyEqual(value->load(), expected.value))
            {
                std::cerr << "Factory preset value failed for "
                          << expected.parameterId << " at index "
                          << presetIndex << '\n';
                return 1;
            }
        }
    }

    std::array<float, expectedParameters.size()> savedValues {};
    for (size_t index = 0; index < expectedParameters.size(); ++index)
    {
        const auto& expected = expectedParameters[index];
        auto* parameter = source.apvts.getParameter(expected.id);
        const float normalised = (index % 3 == 0) ? 0.25f
                               : (index % 3 == 1) ? 0.60f : 0.85f;
        parameter->setValueNotifyingHost(normalised);
        savedValues[index] = source.apvts.getRawParameterValue(
            expected.id)->load();
    }

    juce::MemoryBlock state;
    source.getStateInformation(state);
    QuanpressorAudioProcessor restored;
    restored.setStateInformation(state.getData(),
                                 static_cast<int>(state.getSize()));

    for (size_t index = 0; index < expectedParameters.size(); ++index)
    {
        const auto& expected = expectedParameters[index];
        const float value = restored.apvts.getRawParameterValue(
            expected.id)->load();
        if (!nearlyEqual(value, savedValues[index]))
        {
            std::cerr << "Saved state did not restore " << expected.id
                      << ": " << savedValues[index] << " to " << value << '\n';
            return 1;
        }
    }

    if (restored.getCurrentPresetName()
        != source.getCurrentPresetName())
    {
        std::cerr << "Saved state did not restore the preset name\n";
        return 1;
    }

    std::cout << "PASS: parameters, sidechain buses, presets, and state restore are compatible\n";
    return 0;
}
