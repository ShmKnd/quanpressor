#include "../Source/PluginProcessor.h"

#include <cmath>
#include <iostream>

class GrMatchThresholdOwnershipTestAccess
{
public:
    static void dispatchPendingUpdate(QuanpressorAudioProcessor& processor)
    {
        processor.handleAsyncUpdate();
    }
};

namespace
{
void setPlainValue(juce::AudioProcessorValueTreeState& state,
                   const char* parameterId, float value)
{
    auto* parameter = state.getParameter(parameterId);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void prepareSignal(juce::AudioBuffer<float>& block)
{
    block.setSize(2, 512);
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int i = 0; i < block.getNumSamples(); ++i)
            block.setSample(channel, i, 0.1f);
}

void scheduleGrMatchThresholdWrite(QuanpressorAudioProcessor& processor,
                                   juce::AudioBuffer<float>& block,
                                   juce::MidiBuffer& midi)
{
    setPlainValue(processor.apvts, "grMatch", 1.0f);
    setPlainValue(processor.apvts, "mode", static_cast<float>(QuanpressorDSP::OPTO));
    processor.processBlock(block, midi);
}

bool checkExternalChange(QuanpressorAudioProcessor& processor,
                         float expectedThreshold,
                         const char* enabledMessage,
                         const char* overwrittenMessage)
{
    if (processor.apvts.getRawParameterValue("grMatch")->load() > 0.5f)
    {
        std::cerr << enabledMessage << '\n';
        return false;
    }

    const float threshold = processor.apvts.getRawParameterValue("threshold")->load();
    if (std::abs(threshold - expectedThreshold) > 0.051f)
    {
        std::cerr << overwrittenMessage << ": " << threshold << " dB\n";
        return false;
    }
    return true;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceGui;
    juce::AudioBuffer<float> block;
    juce::MidiBuffer midi;
    prepareSignal(block);

    {
        QuanpressorAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        scheduleGrMatchThresholdWrite(processor, block, midi);
        GrMatchThresholdOwnershipTestAccess::dispatchPendingUpdate(processor);

        if (processor.apvts.getRawParameterValue("grMatch")->load() < 0.5f)
        {
            std::cerr << "GR Match cancelled its own Threshold update\n";
            return 1;
        }

        constexpr float externalThresholdDb = -12.0f;
        setPlainValue(processor.apvts, "threshold", externalThresholdDb);
        GrMatchThresholdOwnershipTestAccess::dispatchPendingUpdate(processor);
        if (!checkExternalChange(processor, externalThresholdDb,
                                 "External Threshold automation did not disable GR Match",
                                 "External Threshold value was overwritten"))
            return 1;
    }

    {
        QuanpressorAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        scheduleGrMatchThresholdWrite(processor, block, midi);

        constexpr float racedThresholdDb = -9.0f;
        setPlainValue(processor.apvts, "threshold", racedThresholdDb);
        GrMatchThresholdOwnershipTestAccess::dispatchPendingUpdate(processor);
        if (!checkExternalChange(processor, racedThresholdDb,
                                 "A pending GR Match write ignored external Threshold automation",
                                 "Pending GR Match write replaced external Threshold"))
            return 1;
    }

    std::cout << "PASS: GR Match owns only its internal Threshold writes and yields to external changes\n";
    return 0;
}
