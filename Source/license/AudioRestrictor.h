#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "LicenseManager.h"

class AudioRestrictor final
{
public:
    static void apply(juce::AudioBuffer<float>& buffer, double sampleRate) noexcept
    {
        if (!LicenseManager::getInstance().isAudioRestricted())
            return;

#if JUCE_IOS
        juce::ignoreUnused(sampleRate);
        buffer.clear();
#else
        if (sampleRate <= 0.0)
            return;

        const auto interval = static_cast<juce::int64>(sampleRate * 30.0);
        const auto duration = static_cast<juce::int64>(sampleRate * 0.5);
        thread_local juce::int64 sampleCounter = 0;
        thread_local juce::Random random;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (sampleCounter % interval < duration)
            {
                const auto noise = (random.nextFloat() * 2.0f - 1.0f) * 0.01f;
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.addSample(channel, sample, noise);
            }
            ++sampleCounter;
        }
#endif
    }
};
