#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LicenseManager.h"

class LicenseComponent final : public juce::Component,
                               private juce::Timer
{
public:
    LicenseComponent()
    {
        statusLabel.setFont(juce::FontOptions(11.5f));
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(statusLabel);

        loadButton.setButtonText("LOAD LICENSE...");
        loadButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        loadButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        loadButton.onClick = [this] { chooseLicenseFile(); };
        addAndMakeVisible(loadButton);

        startTimerHz(1);
        refresh();
    }

    ~LicenseComponent() override { stopTimer(); }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colour(0xe6182028));
        graphics.fillRect(getLocalBounds());
        graphics.setColour(juce::Colour(0xffd9a928).withAlpha(0.7f));
        graphics.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10, 3);
        loadButton.setBounds(area.removeFromRight(126));
        statusLabel.setBounds(area);
    }

private:
    void timerCallback() override { refresh(); }

    void refresh()
    {
        const auto& manager = LicenseManager::getInstance();
        const auto currentState = manager.getState();
        statusLabel.setText(manager.getStatusString(), juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId,
                              currentState == LicenseManager::State::TrialActive
                                  ? juce::Colours::orange
                                  : juce::Colours::orangered);
        setVisible(currentState != LicenseManager::State::Valid);
    }

    void chooseLicenseFile()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Select License File",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.soundravelicense");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& selected)
        {
            const auto source = selected.getResult();
            auto& manager = LicenseManager::getInstance();
            if (source.existsAsFile() && manager.loadFromFile(source)
                && manager.getState() == LicenseManager::State::Valid)
            {
                const auto destination = LicenseManager::getDefaultLicenseFile(manager.getProductName());
                destination.getParentDirectory().createDirectory();
                const bool stored = source == destination || source.copyFileTo(destination);
                juce::AlertWindow::showMessageBoxAsync(
                    stored ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    stored ? "License Activated" : "License Storage Error",
                    stored ? "The license was activated successfully."
                           : "The license is valid, but it could not be copied to application storage.");
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "License Error",
                                                       "The selected license is invalid or expired.");
            }
            refresh();
        });
    }

    juce::Label statusLabel;
    juce::TextButton loadButton;
    std::unique_ptr<juce::FileChooser> chooser;
};
