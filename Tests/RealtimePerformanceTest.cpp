#include "../Source/QuanpressorDSP.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

namespace
{
constexpr int blockSize = 64;
constexpr double measuredSeconds = 3.0;

const char* modeName(QuanpressorDSP::Mode mode)
{
    switch (mode)
    {
        case QuanpressorDSP::FET: return "FET";
        case QuanpressorDSP::OPTO: return "OPTO";
        case QuanpressorDSP::VCA: return "VCA";
        case QuanpressorDSP::VARIMU: return "VARIMU";
    }
    return "UNKNOWN";
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
    constexpr std::array<double, 3> sampleRates {
        44100.0, 48000.0, 96000.0
    };
    constexpr std::array<int, 2> channelCounts { 1, 2 };
    std::cout << "mode,sample_rate,channels,block_size,quality,audio_seconds,"
                 "elapsed_seconds,realtime_factor\n";

    for (const double sampleRate : sampleRates)
    {
        for (const int channels : channelCounts)
        {
            for (const auto mode : modes)
            {
                QuanpressorDSP dsp;
                dsp.prepare(sampleRate, blockSize, channels);
                QuanpressorDSP::Controls controls;
                controls.mode = mode;
                controls.thresholdDb = -30.0f;
                controls.ratio = 8.0f;
                controls.driveDb = 18.0f;
                controls.oversampling = true;
                controls.oversamplingFactor = 4;
                controls.stereoChain = true;
                juce::AudioBuffer<float> block(channels, blockSize);
                int sampleOffset = 0;

                const auto processBlock = [&]
                {
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        auto* data = block.getWritePointer(channel);
                        for (int sample = 0; sample < blockSize; ++sample)
                        {
                            const double time = (sampleOffset + sample)
                                / sampleRate;
                            data[sample] = static_cast<float>(
                                0.18 * std::sin(
                                    juce::MathConstants<double>::twoPi
                                    * 997.0 * time)
                                + 0.07 * std::sin(
                                    juce::MathConstants<double>::twoPi
                                    * 10003.0 * time));
                        }
                    }
                    dsp.process(block, controls);
                    sampleOffset += blockSize;
                };

                const int warmupBlocks = static_cast<int>(
                    0.20 * sampleRate / blockSize);
                for (int blockIndex = 0;
                     blockIndex < warmupBlocks;
                     ++blockIndex)
                {
                    processBlock();
                }

                const int measuredBlocks = static_cast<int>(
                    measuredSeconds * sampleRate / blockSize);
                const auto start = std::chrono::steady_clock::now();
                for (int blockIndex = 0;
                     blockIndex < measuredBlocks;
                     ++blockIndex)
                {
                    processBlock();
                }
                const auto end = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(
                    end - start).count();

                bool finite = true;
                for (int channel = 0; channel < channels; ++channel)
                    for (int sample = 0; sample < blockSize; ++sample)
                        finite = finite
                            && std::isfinite(block.getSample(channel, sample));
                if (!finite || elapsed <= 0.0)
                {
                    std::cerr
                        << "Performance run produced an invalid result\n";
                    return 1;
                }

                std::cout << modeName(mode) << ',' << sampleRate << ','
                          << channels << ',' << blockSize
                          << ",4x," << measuredSeconds << ','
                          << elapsed << ','
                          << measuredSeconds / elapsed << '\n';
            }
        }
    }
    return 0;
}
