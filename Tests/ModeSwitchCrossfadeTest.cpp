#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.oversampling = false;
    controls.driveDb = 12.0f;
    controls.thresholdDb = 0.0f;
    juce::AudioBuffer<float> block(2, blockSize);
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int i = 0; i < block.getNumSamples(); ++i)
            block.setSample(channel, i, 0.25f * std::sin(
                juce::MathConstants<double>::twoPi * 997.0 * i / sampleRate));
    dsp.process(block, controls);

    controls.mode = QuanpressorDSP::OPTO;
    dsp.process(block, controls);
    if (std::abs(dsp.getModeTransitionTrimDb()) < 1.0e-6f)
    {
        std::cerr << "Expected a temporary level trim during a signal-bearing crossfade\n";
        return 1;
    }
    const int expectedAfterFirstBlock = juce::roundToInt(
        QuanpressorDSP::modeCrossfadeSeconds * sampleRate) - blockSize;
    if (dsp.getModeCrossfadeRemainingSamples() != expectedAfterFirstBlock)
    {
        std::cerr << "Unexpected crossfade duration after mode switch: "
                  << dsp.getModeCrossfadeRemainingSamples() << " samples remain\n";
        return 1;
    }

    while (dsp.getModeCrossfadeRemainingSamples() > 0)
        dsp.process(block, controls);

    if (dsp.getModeTransitionTrimDb() != 0.0f)
    {
        std::cerr << "Mode transition trim persisted after the crossfade: "
                  << dsp.getModeTransitionTrimDb() << " dB\n";
        return 1;
    }

    const int expectedTotalSamples = juce::roundToInt(
        QuanpressorDSP::modeCrossfadeSeconds * sampleRate);
    if (expectedTotalSamples != 1440)
    {
        std::cerr << "Expected a 1440-sample crossfade at 48 kHz, got "
                  << expectedTotalSamples << '\n';
        return 1;
    }

    std::cout << "PASS: mode crossfade completes in " << expectedTotalSamples
              << " samples (30 ms at 48 kHz)\n";
    return 0;
}
