#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int preparedBlockSize = 512;
constexpr int renderedSamples = 70000;

void fillTestSignal(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            samples[i] = 0.1f * std::sin(juce::MathConstants<double>::twoPi * 1000.0
                                        * static_cast<double>(i) / sampleRate);
    }
}
}

int main()
{
    QuanpressorDSP wholeBlockDsp;
    QuanpressorDSP referenceDsp;
    wholeBlockDsp.prepare(sampleRate, preparedBlockSize, 2);
    referenceDsp.prepare(sampleRate, preparedBlockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::VCA;
    controls.thresholdDb = 0.0f;
    controls.outputDb = -6.0f;
    controls.oversampling = false;

    juce::AudioBuffer<float> source(2, renderedSamples);
    fillTestSignal(source);

    juce::AudioBuffer<float> wholeBlock(source);
    juce::AudioBuffer<float> reference(source);

    wholeBlockDsp.process(wholeBlock, controls);
    for (int offset = 0; offset < renderedSamples; offset += preparedBlockSize)
    {
        const int samples = juce::jmin(preparedBlockSize, renderedSamples - offset);
        juce::AudioBuffer<float> chunk(reference.getArrayOfWritePointers(), 2, offset, samples);
        referenceDsp.process(chunk, controls);
    }

    float largestDifference = 0.0f;
    float processedTailDifference = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int i = 0; i < renderedSamples; ++i)
            largestDifference = juce::jmax(largestDifference,
                std::abs(wholeBlock.getSample(channel, i) - reference.getSample(channel, i)));

        for (int i = renderedSamples - 1024; i < renderedSamples; ++i)
            processedTailDifference = juce::jmax(processedTailDifference,
                std::abs(wholeBlock.getSample(channel, i) - source.getSample(channel, i)));
    }

    if (largestDifference > 1.0e-6f)
    {
        std::cerr << "Large-block output diverged from prepared-size processing: "
                  << largestDifference << '\n';
        return 1;
    }

    if (processedTailDifference < 1.0e-3f)
    {
        std::cerr << "The tail of the oversized block was left unprocessed\n";
        return 1;
    }

    std::cout << "PASS: 70000-sample block matches 512-sample reference processing; max error "
              << largestDifference << '\n';
    return 0;
}
