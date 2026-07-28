#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 4096;

bool checkRamp(float start, float end)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 1);
    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::VARIMU;
    controls.thresholdDb = 0.0f;
    controls.kneeDb = 2.0f;
    controls.driveDb = 0.0f;
    controls.oversampling = false;

    juce::AudioBuffer<float> block(1, blockSize);
    block.clear();
    dsp.process(block, controls);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const float position = static_cast<float>(sample)
            / static_cast<float>(blockSize - 1);
        block.setSample(0, sample, start + position * (end - start));
    }
    dsp.process(block, controls);

    const int latency = dsp.getLatencySamples();
    float previous = block.getSample(0, latency);
    float maximumStep = 0.0f;
    for (int sample = latency + 1; sample < blockSize; ++sample)
    {
        const float current = block.getSample(0, sample);
        if (!std::isfinite(current) || current + 1.0e-6f < previous)
        {
            std::cerr << "Ramp failed at sample " << sample
                      << ": previous=" << previous
                      << ", current=" << current
                      << ", GR=" << dsp.getInstantaneousGrDb() << '\n';
            return false;
        }
        maximumStep = juce::jmax(
            maximumStep, std::abs(current - previous));
        previous = current;
    }
    if (maximumStep >= 0.001f
        || std::abs(dsp.getInstantaneousGrDb()) >= 1.0e-6f)
    {
        std::cerr << "Ramp metrics failed: max step=" << maximumStep
                  << ", GR=" << dsp.getInstantaneousGrDb() << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    if (!checkRamp(0.0f, 0.75f)
        || !checkRamp(-0.75f, 0.0f))
    {
        std::cerr
            << "Vari-Mu push-pull transfer is not continuous and monotonic\n";
        return 1;
    }

    std::cout
        << "PASS: Vari-Mu push-pull transfer is continuous at Drive 0\n";
    return 0;
}
