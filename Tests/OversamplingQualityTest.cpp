#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr double pi = 3.14159265358979323846;

double toneProjection(
    const std::vector<float>& signal,
    double frequency) noexcept
{
    double real = 0.0;
    double imaginary = 0.0;
    double windowSum = 0.0;
    for (size_t sample = 0; sample < signal.size(); ++sample)
    {
        const double phase = 2.0 * pi * frequency
            * static_cast<double>(sample) / sampleRate;
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(sample)
            / static_cast<double>(signal.size() - 1));
        real += signal[sample] * window * std::cos(phase);
        imaginary -= signal[sample] * window * std::sin(phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt(real * real + imaginary * imaginary)
        / windowSum;
}

double foldedFrequency(double frequency) noexcept
{
    frequency = std::fmod(std::abs(frequency), sampleRate);
    return frequency > sampleRate * 0.5
        ? sampleRate - frequency : frequency;
}

double aliasEnergyDb(
    QuanpressorDSP::Mode mode,
    bool oversampling) noexcept
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.thresholdDb = -24.0f;
    controls.ratio = 4.0f;
    controls.attackMs = 10.0f;
    controls.releaseMs = 150.0f;
    controls.kneeDb = 6.0f;
    controls.driveDb = 18.0f;
    controls.oversampling = oversampling;
    controls.oversamplingFactor = 4;

    constexpr int settleSamples = 12000;
    constexpr int captureSamples = 8192;
    constexpr int totalSamples = settleSamples + captureSamples;
    juce::AudioBuffer<float> block(2, blockSize);
    std::vector<float> captured;
    captured.reserve(captureSamples);

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int samples = juce::jmin(blockSize, totalSamples - offset);
        juce::AudioBuffer<float> view(
            block.getArrayOfWritePointers(), 2, 0, samples);
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = view.getWritePointer(channel);
            for (int sample = 0; sample < samples; ++sample)
            {
                data[sample] = static_cast<float>(
                    0.35 * std::sin(
                        2.0 * pi * 10000.0
                        * (offset + sample) / sampleRate));
            }
        }
        dsp.process(view, controls);
        if (offset + samples > settleSamples)
        {
            const int captureStart = juce::jmax(
                0, settleSamples - offset);
            for (int sample = captureStart; sample < samples; ++sample)
                captured.push_back(view.getSample(0, sample));
        }
    }

    const double fundamental = toneProjection(captured, 10000.0);
    double aliasEnergy = 0.0;
    for (int harmonic = 2; harmonic <= 12; ++harmonic)
    {
        const double frequency = foldedFrequency(10000.0 * harmonic);
        if (frequency < 20.0 || std::abs(frequency - 10000.0) < 1.0)
            continue;
        const double amplitude = toneProjection(captured, frequency);
        aliasEnergy += amplitude * amplitude;
    }
    return 20.0 * std::log10(
        std::max(std::sqrt(aliasEnergy) / fundamental, 1.0e-12));
}
}

int main()
{
    QuanpressorDSP latencyProbe;
    latencyProbe.prepare(sampleRate, blockSize, 2);
    if (latencyProbe.getLatencySamples() != 92)
    {
        std::cerr << "Unexpected High-quality fixed latency: "
                  << latencyProbe.getLatencySamples() << '\n';
        return 1;
    }

    constexpr std::array<QuanpressorDSP::Mode, 4> modes {
        QuanpressorDSP::FET,
        QuanpressorDSP::OPTO,
        QuanpressorDSP::VCA,
        QuanpressorDSP::VARIMU
    };
    constexpr std::array<double, 4> maximumAliasDb {
        -55.0, -55.0, -65.0, -45.0
    };

    for (size_t index = 0; index < modes.size(); ++index)
    {
        const double nativeAlias = aliasEnergyDb(modes[index], false);
        const double oversampledAlias = aliasEnergyDb(modes[index], true);
        if (!std::isfinite(oversampledAlias)
            || oversampledAlias > maximumAliasDb[index])
        {
            std::cerr << "4x alias target failed for mode " << index
                      << ": native=" << nativeAlias
                      << " dBc, 4x=" << oversampledAlias << " dBc\n";
            return 1;
        }
        if (index != static_cast<size_t>(QuanpressorDSP::VCA)
            && oversampledAlias > nativeAlias - 20.0)
        {
            std::cerr << "4x alias reduction was too small for mode "
                      << index << '\n';
            return 1;
        }
    }

    std::cout << "PASS: High-quality 4x paths meet latency and alias targets\n";
    return 0;
}
