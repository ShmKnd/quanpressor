#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

float measureSteadyGr(float ratio)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::OPTO;
    controls.thresholdDb = -24.0f;
    controls.ratio = ratio;
    controls.oversampling = false;
    controls.stereoChain = false;

    juce::AudioBuffer<float> block(2, blockSize);
    int sampleOffset = 0;
    for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
    {
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getWritePointer(channel);
            for (int i = 0; i < blockSize; ++i)
                samples[i] = 0.5f * std::sin(juce::MathConstants<double>::twoPi * 1000.0
                    * (sampleOffset + i) / sampleRate);
        }
        dsp.process(block, controls);
        sampleOffset += blockSize;
    }
    return dsp.getGrDb();
}
}

int main()
{
    constexpr std::array<float, 6> ratios { 4.0f, 7.99f, 8.0f, 8.01f, 12.0f, 20.0f };
    std::array<float, ratios.size()> reductions {};
    for (size_t i = 0; i < ratios.size(); ++i)
        reductions[i] = measureSteadyGr(ratios[i]);

    const float boundaryJump = std::abs(reductions[2] - reductions[1]);
    if (boundaryJump > 0.1f)
    {
        std::cerr << "OPTO Ratio jumps at 8:1 by " << boundaryJump << " dB ("
                  << reductions[1] << " to " << reductions[2] << " dB)\n";
        return 1;
    }

    for (size_t i = 1; i < reductions.size(); ++i)
    {
        if (reductions[i] > reductions[i - 1] + 0.02f)
        {
            std::cerr << "OPTO gain reduction is not monotonic between ratios "
                      << ratios[i - 1] << " and " << ratios[i] << '\n';
            return 1;
        }
    }

    std::cout << "PASS: OPTO Ratio is continuous at 8:1; boundary jump "
              << boundaryJump << " dB\n";
    return 0;
}
