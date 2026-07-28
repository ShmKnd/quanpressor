#include "../Source/PluginProcessor.h"

#include <array>
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

juce::Slider* findSlider(juce::Component& component, const juce::String& componentId)
{
    if (component.getComponentID() == componentId)
        return dynamic_cast<juce::Slider*>(&component);

    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* slider = findSlider(*component.getChildComponent(i), componentId))
            return slider;

    return nullptr;
}
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

        auto* attack = findSlider(*editor, "attack");
        if (attack == nullptr)
        {
            std::cerr << "Attack slider is missing\n";
            return 1;
        }

        const std::array<std::pair<double, juce::String>, 5> popupCases {{
            { 0.02, "0.02 ms" },
            { 10.0, "0.27 ms" },
            { 30.0, "0.45 ms" },
            { 60.0, "0.62 ms" },
            { 100.0, "0.80 ms" }
        }};
        for (const auto& [parameterValue, expectedText] : popupCases)
        {
            const auto actualText = attack->getTextFromValue(parameterValue);
            if (actualText != expectedText)
            {
                std::cerr << "FET Attack popup displayed " << actualText
                          << " instead of " << expectedText << '\n';
                return 1;
            }
        }
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
              << ", resolved FET DSP to 4:1, and displayed mapped Attack time\n";
    return 0;
}
