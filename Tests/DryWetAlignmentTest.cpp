#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

bool checkPath(QuanpressorDSP::Mode mode, bool oversampling, int factor)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.mix = 0.0f;
    controls.driveDb = 18.0f;
    controls.oversampling = oversampling;
    controls.oversamplingFactor = factor;

    juce::AudioBuffer<float> block(2, blockSize);
    block.clear();
    block.setSample(0, 0, 1.0f);
    block.setSample(1, 0, -0.5f);
    dsp.process(block, controls);

    const int latency = dsp.getLatencySamples();
    if (latency < 0 || latency >= blockSize)
        return false;

    for (int channel = 0; channel < 2; ++channel)
    {
        const float expected = channel == 0 ? 1.0f : -0.5f;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const float target = sample == latency ? expected : 0.0f;
            if (std::abs(block.getSample(channel, sample) - target) > 1.0e-6f)
                return false;
        }
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
        if (!checkPath(mode, false, 2)
            || !checkPath(mode, true, 2)
            || !checkPath(mode, true, 4))
        {
            std::cerr << "Dry path latency alignment failed for mode "
                      << static_cast<int>(mode) << '\n';
            return 1;
        }
    }

    std::cout << "PASS: dry path is sample-aligned for every mode and quality\n";
    return 0;
}
