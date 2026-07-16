#include "../Source/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace
{
struct ParameterChangeCounter final : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged(int, float) override { ++valueChanges; }
    void parameterGestureChanged(int, bool) override {}

    int valueChanges = 0;
};
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceGui;
    QuanpressorAudioProcessor processor;
    auto* ratio = processor.apvts.getParameter("ratio");
    if (ratio == nullptr)
    {
        std::cerr << "Ratio parameter is missing\n";
        return 1;
    }

    constexpr float automatedRatio = 5.9f;
    ParameterChangeCounter changeCounter;
    ratio->addListener(&changeCounter);
    ratio->setValueNotifyingHost(ratio->convertTo0to1(automatedRatio));
    changeCounter.valueChanges = 0;
    const float beforeEditor = processor.apvts.getRawParameterValue("ratio")->load();

    float whileEditorIsOpen = 0.0f;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        whileEditorIsOpen = processor.apvts.getRawParameterValue("ratio")->load();
    }

    ratio->removeListener(&changeCounter);
    if (std::abs(whileEditorIsOpen - beforeEditor) > 1.0e-4f)
    {
        std::cerr << "Opening the editor changed Ratio from " << beforeEditor
                  << " to " << whileEditorIsOpen << '\n';
        return 1;
    }

    if (changeCounter.valueChanges != 0)
    {
        std::cerr << "Opening the editor emitted " << changeCounter.valueChanges
                  << " Ratio parameter change notification(s)\n";
        return 1;
    }

    const float afterEditor = processor.apvts.getRawParameterValue("ratio")->load();
    if (std::abs(afterEditor - beforeEditor) > 1.0e-4f)
    {
        std::cerr << "Closing the editor changed Ratio from " << beforeEditor
                  << " to " << afterEditor << '\n';
        return 1;
    }

    if (QuanpressorDSP::fetRatioValue(beforeEditor) != 4.0f)
    {
        std::cerr << "5.9 did not resolve to the expected 4:1 FET ratio\n";
        return 1;
    }

    std::cout << "PASS: Editor preserved Ratio " << afterEditor
              << " while FET DSP resolved it to 4:1\n";
    return 0;
}
