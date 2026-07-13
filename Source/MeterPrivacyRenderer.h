#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

class MeterPrivacyRenderer
{
public:
    static constexpr size_t regionCount = 3;
    using Regions = std::array<juce::Rectangle<int>, regionCount>;

    virtual ~MeterPrivacyRenderer() = default;

    virtual const char* getBackendName() const noexcept = 0;
    virtual juce::Component& getView() noexcept = 0;
    virtual void attach() = 0;
    virtual void shutdown() = 0;
    virtual void setEnabled(bool shouldBeEnabled) = 0;
    virtual void submitFrame(juce::Image source, Regions regions, float blurRadius,
                             juce::Image foreground = {}) = 0;
};

std::unique_ptr<MeterPrivacyRenderer> createMeterPrivacyRenderer();
