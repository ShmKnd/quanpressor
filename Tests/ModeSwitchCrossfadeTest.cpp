#include "../Source/QuanpressorDSP.h"

#include <iostream>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.oversampling = false;
    juce::AudioBuffer<float> block(2, blockSize);
    block.clear();
    dsp.process(block, controls);

    controls.mode = QuanpressorDSP::OPTO;
    dsp.process(block, controls);
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
