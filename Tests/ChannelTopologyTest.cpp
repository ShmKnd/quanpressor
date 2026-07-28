#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

bool runTopology(QuanpressorDSP::Mode mode, int channels, bool linked)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, channels);
    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.attackMs = 1.0f;
    controls.releaseMs = 300.0f;
    controls.stereoChain = linked;
    controls.oversampling = false;
    juce::AudioBuffer<float> block(channels, blockSize);
    int sampleOffset = 0;

    for (int blockIndex = 0; blockIndex < 1000; ++blockIndex)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const float amplitude = channel == 0 ? 0.5f : 0.08f;
            auto* data = block.getWritePointer(channel);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                data[sample] = static_cast<float>(
                    amplitude * std::sin(
                        juce::MathConstants<double>::twoPi * 1000.0
                        * (sampleOffset + sample) / sampleRate));
            }
        }
        dsp.process(block, controls);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                if (!std::isfinite(block.getSample(channel, sample)))
                    return false;
        sampleOffset += blockSize;
    }

    if (channels == 2 && linked)
    {
        if (std::abs(dsp.getGrDbForChannel(0) - dsp.getGrDbForChannel(1)) > 0.05f)
            return false;
    }
    if (channels == 2 && !linked)
    {
        if (!(dsp.getGrDbForChannel(0) < dsp.getGrDbForChannel(1) - 0.10f))
            return false;
    }
    return true;
}
}

int main()
{
    constexpr std::array<QuanpressorDSP::Mode, 4> modes {
        QuanpressorDSP::FET,
        QuanpressorDSP::OPTO,
        QuanpressorDSP::VCA,
        QuanpressorDSP::VARIMU
    };
    for (const auto mode : modes)
    {
        if (!runTopology(mode, 1, true)
            || !runTopology(mode, 2, true)
            || !runTopology(mode, 2, false))
        {
            std::cerr << "Channel topology failed for mode "
                      << static_cast<int>(mode) << '\n';
            return 1;
        }
    }
    std::cout << "PASS: mono, stereo-linked, and dual-mono operation in all modes\n";
    return 0;
}
