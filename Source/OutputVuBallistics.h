#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

class OutputVuBallistics
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = juce::jmax(1.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        meanSquare = 0.0;
        valueDb = -60.0f;
    }

    float process(double finalOutputMeanSquare, int numSamples) noexcept
    {
        finalOutputMeanSquare = juce::jmax(0.0, finalOutputMeanSquare);
        const double timeSeconds = finalOutputMeanSquare > meanSquare ? 0.065 : 0.300;
        const double alpha = 1.0 - std::exp(-static_cast<double>(numSamples)
                                             / (sampleRate * timeSeconds));
        meanSquare += alpha * (finalOutputMeanSquare - meanSquare);
        valueDb = juce::jlimit(-60.0f, 3.0f,
            static_cast<float>(10.0 * std::log10(juce::jmax(meanSquare, 1.0e-20))
                               + referenceCalibrationDb));
        return valueDb;
    }

    float getValueDb() const noexcept { return valueDb; }

private:
    // Quanpressor calibration contract: a 1 kHz sine whose sample peak is
    // -18 dBFS reads 0 VU. Its mean-square level is -21.0102999566 dBFS.
    static constexpr double referenceCalibrationDb = 21.0102999566;
    double sampleRate = 44100.0;
    double meanSquare = 0.0;
    float valueDb = -60.0f;
};
