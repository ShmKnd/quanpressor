#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

struct Measurement
{
    float gainReductionDb = 0.0f;
    float outputPeak = 0.0f;
    float leftGainReductionDb = 0.0f;
    float rightGainReductionDb = 0.0f;
};

Measurement run(QuanpressorDSP::Mode mode,
                bool useExternalSidechain, int sidechainChannels,
                bool stereoLinked)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);

    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.thresholdDb = -24.0f;
    controls.ratio = 8.0f;
    controls.kneeDb = 3.0f;
    controls.attackMs = 1.0f;
    controls.releaseMs = 250.0f;
    controls.stereoChain = stereoLinked;
    controls.oversampling = false;

    juce::AudioBuffer<float> main(2, blockSize);
    juce::AudioBuffer<float> sidechain(
        juce::jmax(1, sidechainChannels), blockSize);
    Measurement result;

    for (int blockIndex = 0; blockIndex < 900; ++blockIndex)
    {
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                main.setSample(channel, sample, 0.02f);

        for (int channel = 0; channel < sidechain.getNumChannels(); ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                sidechain.setSample(channel, sample, 0.8f);

        dsp.process(main, controls,
                    useExternalSidechain ? &sidechain : nullptr);
        if (blockIndex > 800)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                    result.outputPeak = juce::jmax(
                        result.outputPeak,
                        std::abs(main.getSample(channel, sample)));
        }
    }

    result.gainReductionDb = dsp.getInstantaneousGrDb();
    result.leftGainReductionDb = dsp.getGrDbForChannel(0);
    result.rightGainReductionDb = dsp.getGrDbForChannel(1);
    return result;
}
}

int main()
{
    constexpr std::array<QuanpressorDSP::Mode, 4> modes {{
        QuanpressorDSP::FET,
        QuanpressorDSP::OPTO,
        QuanpressorDSP::VCA,
        QuanpressorDSP::VARIMU
    }};
    for (const auto mode : modes)
    {
        const auto internal = run(mode, false, 1, true);
        const auto external = run(mode, true, 1, true);
        if (internal.gainReductionDb < -0.25f)
        {
            std::cerr << "Quiet main input unexpectedly triggered mode "
                      << static_cast<int>(mode) << ": "
                      << internal.gainReductionDb << " dB\n";
            return 1;
        }
        if (external.gainReductionDb > -3.0f)
        {
            std::cerr << "External sidechain did not drive mode "
                      << static_cast<int>(mode) << ": "
                      << external.gainReductionDb << " dB\n";
            return 1;
        }
        if (external.outputPeak > 0.08f)
        {
            std::cerr << "External sidechain leaked into mode "
                      << static_cast<int>(mode) << " output: "
                      << external.outputPeak << '\n';
            return 1;
        }
    }

    const auto dualMonoDetector = run(
        QuanpressorDSP::VCA, true, 1, false);
    if (!std::isfinite(dualMonoDetector.leftGainReductionDb)
        || !std::isfinite(dualMonoDetector.rightGainReductionDb)
        || std::abs(dualMonoDetector.leftGainReductionDb
                    - dualMonoDetector.rightGainReductionDb) > 0.05f)
    {
        std::cerr << "Mono sidechain did not map consistently to stereo: "
                  << dualMonoDetector.leftGainReductionDb << ", "
                  << dualMonoDetector.rightGainReductionDb << " dB\n";
        return 1;
    }

    std::cout
        << "PASS: external sidechain drives detection without leaking into audio\n";
    return 0;
}
