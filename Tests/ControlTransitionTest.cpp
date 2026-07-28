#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

void fillTone(juce::AudioBuffer<float>& block, int sampleOffset)
{
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        auto* data = block.getWritePointer(channel);
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            data[sample] = static_cast<float>(
                0.2 * std::sin(
                    juce::MathConstants<double>::twoPi * 997.0
                    * (sampleOffset + sample) / sampleRate));
        }
    }
}
}

int main()
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::VCA;
    controls.thresholdDb = -24.0f;
    controls.oversampling = false;

    juce::AudioBuffer<float> block(2, blockSize);
    int sampleOffset = 0;
    float previous = 0.0f;
    float maximumJump = 0.0f;
    bool finite = true;

    for (int blockIndex = 0; blockIndex < 500; ++blockIndex)
    {
        if (blockIndex == 100)
        {
            controls.inputDb = 18.0f;
            controls.outputDb = -18.0f;
            controls.makeupDb = 12.0f;
            controls.mix = 0.25f;
            controls.driveDb = 18.0f;
            controls.thresholdDb = -42.0f;
            controls.ratio = 20.0f;
            controls.kneeDb = 18.0f;
            controls.attackMs = 0.02f;
            controls.releaseMs = 1200.0f;
        }
        if (blockIndex == 220)
        {
            controls.oversampling = true;
            controls.oversamplingFactor = 2;
        }
        if (blockIndex == 340)
            controls.oversamplingFactor = 4;

        fillTone(block, sampleOffset);
        dsp.process(block, controls);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const float value = block.getSample(0, sample);
            finite = finite && std::isfinite(value);
            maximumJump = juce::jmax(maximumJump, std::abs(value - previous));
            previous = value;
        }
        if (dsp.getInstantaneousGrDb() > 1.0e-5f)
        {
            std::cerr << "Attenuation envelope exceeded unity\n";
            return 1;
        }
        sampleOffset += blockSize;
    }

    if (!finite || maximumJump > 0.65f)
    {
        std::cerr << "Control or quality automation produced a discontinuity: "
                  << maximumJump << '\n';
        return 1;
    }

    if (dsp.getQualityCrossfadeRemainingSamples() != 0)
    {
        std::cerr << "Quality transition did not complete\n";
        return 1;
    }

    std::cout << "PASS: continuous controls and quality changes remain bounded; max jump "
              << maximumJump << '\n';
    return 0;
}
