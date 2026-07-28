#include "../Source/QuanpressorDSP.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

void fillConstant(juce::AudioBuffer<float>& block, float value)
{
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            block.setSample(channel, sample, value);
}

void settle(QuanpressorDSP& dsp,
            QuanpressorDSP::Controls& controls,
            juce::AudioBuffer<float>& block)
{
    for (int index = 0; index < 750; ++index)
    {
        fillConstant(block, 0.5f);
        dsp.process(block, controls);
    }
}
}

int main()
{
    juce::AudioBuffer<float> block(2, blockSize);

    QuanpressorDSP fet;
    fet.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls fetControls;
    fetControls.mode = QuanpressorDSP::FET;
    fetControls.thresholdDb = -24.0f;
    fetControls.ratio = 4.0f;
    fetControls.attackMs = 0.02f;
    fetControls.oversampling = false;
    settle(fet, fetControls, block);
    const float before = fet.getInstantaneousGrDb();
    fetControls.ratio = 20.0f;
    fillConstant(block, 0.5f);
    fet.process(block, fetControls);
    const float firstBlock = fet.getInstantaneousGrDb();
    if (fet.getDiscreteControlCrossfadeRemainingSamples(
            QuanpressorDSP::FET) <= 0
        || std::abs(firstBlock - before) > 10.0f)
    {
        std::cerr << "FET ratio selection did not enter a bounded crossfade\n";
        return 1;
    }
    for (int index = 0; index < 10; ++index)
    {
        fillConstant(block, 0.5f);
        fet.process(block, fetControls);
    }
    if (fet.getDiscreteControlCrossfadeRemainingSamples(
            QuanpressorDSP::FET) != 0
        || !(fet.getInstantaneousGrDb() < before - 0.5f))
    {
        std::cerr << "FET ratio crossfade did not complete at the new ratio\n";
        return 1;
    }

    QuanpressorDSP variableMu;
    variableMu.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls muControls;
    muControls.mode = QuanpressorDSP::VARIMU;
    muControls.thresholdDb = -30.0f;
    muControls.varMuSpeed = 0;
    muControls.oversampling = false;
    settle(variableMu, muControls, block);
    muControls.varMuSpeed = 5;
    fillConstant(block, 0.001f);
    variableMu.process(block, muControls);
    if (variableMu.getDiscreteControlCrossfadeRemainingSamples(
            QuanpressorDSP::VARIMU) <= 0)
    {
        std::cerr << "Vari-mu time-constant selection did not enter a crossfade\n";
        return 1;
    }
    for (int index = 0; index < 10; ++index)
    {
        fillConstant(block, 0.001f);
        variableMu.process(block, muControls);
    }
    if (variableMu.getDiscreteControlCrossfadeRemainingSamples(
            QuanpressorDSP::VARIMU) != 0
        || !std::isfinite(variableMu.getInstantaneousGrDb()))
    {
        std::cerr << "Vari-mu time-constant crossfade did not complete safely\n";
        return 1;
    }

    std::cout << "PASS: FET ratio and Vari-mu time-constant changes crossfade safely\n";
    return 0;
}
