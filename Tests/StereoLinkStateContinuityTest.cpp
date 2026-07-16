#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

void fillSignal(juce::AudioBuffer<float>& block, int sampleOffset)
{
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        auto* samples = block.getWritePointer(channel);
        for (int i = 0; i < block.getNumSamples(); ++i)
            samples[i] = 0.35f * std::sin(juce::MathConstants<double>::twoPi * 997.0
                * (sampleOffset + i) / sampleRate);
    }
}
}

int main()
{
    QuanpressorDSP unlinkedReference;
    QuanpressorDSP linkToggled;
    unlinkedReference.prepare(sampleRate, blockSize, 2);
    linkToggled.prepare(sampleRate, blockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::OPTO;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.oversampling = false;
    controls.stereoChain = false;

    juce::AudioBuffer<float> source(2, blockSize);
    juce::AudioBuffer<float> referenceBlock(2, blockSize);
    juce::AudioBuffer<float> toggledBlock(2, blockSize);
    int sampleOffset = 0;

    for (int blockIndex = 0; blockIndex < 500; ++blockIndex)
    {
        fillSignal(source, sampleOffset);
        referenceBlock.makeCopyOf(source);
        toggledBlock.makeCopyOf(source);
        unlinkedReference.process(referenceBlock, controls);
        linkToggled.process(toggledBlock, controls);
        sampleOffset += blockSize;
    }

    if (linkToggled.getGrDb() > -0.5f)
    {
        std::cerr << "OPTO did not build enough photocell memory for the test\n";
        return 1;
    }

    fillSignal(source, sampleOffset);
    referenceBlock.makeCopyOf(source);
    toggledBlock.makeCopyOf(source);
    unlinkedReference.process(referenceBlock, controls);
    controls.stereoChain = true;
    linkToggled.process(toggledBlock, controls);

    float largestDifference = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < blockSize; ++i)
            largestDifference = juce::jmax(largestDifference,
                std::abs(referenceBlock.getSample(channel, i)
                         - toggledBlock.getSample(channel, i)));

    if (largestDifference > 1.0e-4f)
    {
        std::cerr << "Stereo Link reset or discontinuously changed OPTO state; max difference "
                  << largestDifference << '\n';
        return 1;
    }

    const int expectedRemaining = juce::roundToInt(
        QuanpressorDSP::stereoLinkCrossfadeSeconds * sampleRate) - blockSize;
    if (linkToggled.getStereoLinkCrossfadeRemainingSamples() != expectedRemaining)
    {
        std::cerr << "Unexpected Stereo Link crossfade duration\n";
        return 1;
    }

    std::cout << "PASS: Stereo Link preserves OPTO state and transitions over 5 ms; max difference "
              << largestDifference << '\n';
    return 0;
}
