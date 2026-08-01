#pragma once

#include "IAPManager.h"
#include "IAPProductConfig.h"

class IAPComponent final : public juce::Component,
                           private juce::ChangeListener
{
private:
    class PurchaseLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit PurchaseLookAndFeel(juce::Colour accentToUse)
            : accent(accentToUse)
        {
        }

        void drawButtonBackground(juce::Graphics& graphics,
                                  juce::Button& button,
                                  const juce::Colour&,
                                  bool isMouseOver,
                                  bool isButtonDown) override
        {
            const bool enabled = button.isEnabled();
            const bool primary = static_cast<bool>(
                button.getProperties().getWithDefault("iapPrimary", false));
            const auto bounds = button.getLocalBounds().toFloat().reduced(0.75f);

            auto fill = juce::Colour::fromRGB(8, 9, 10);
            if (primary && enabled)
                fill = isButtonDown ? accent.darker(0.52f)
                                    : isMouseOver ? accent.darker(0.64f)
                                                  : accent.darker(0.74f);
            else if (isButtonDown && enabled)
                fill = juce::Colour::fromRGB(22, 24, 26);

            graphics.setColour(fill);
            graphics.fillRect(bounds);
            graphics.setColour(enabled ? accent.brighter(0.18f)
                                       : juce::Colour::fromRGB(98, 111, 114));
            graphics.drawRect(bounds, 1.5f);
        }

        void drawButtonText(juce::Graphics& graphics,
                            juce::TextButton& button,
                            bool,
                            bool isButtonDown) override
        {
            const auto textLength = juce::jmax(1, button.getButtonText().length());
            const auto widthLimitedHeight = (float) button.getWidth() / ((float) textLength * 0.62f);
            auto font = juce::Font(juce::FontOptions(
                juce::Font::getDefaultMonospacedFontName(),
                juce::jlimit(8.0f, 13.0f,
                             juce::jmin(button.getHeight() * 0.38f, widthLimitedHeight)),
                juce::Font::bold));
            font.setHorizontalScale(1.0f);

            graphics.setFont(font);
            graphics.setColour(button.isEnabled()
                                   ? juce::Colour::fromRGB(244, 250, 251)
                                   : juce::Colour::fromRGB(153, 163, 165));
            graphics.drawText(button.getButtonText(),
                              button.getLocalBounds().reduced(2),
                              juce::Justification::centred,
                              false);
        }

    private:
        juce::Colour accent;
    };

public:
    IAPComponent()
        : accentColour(juce::Colour::fromRGB(
              soundrave::iap::product_config::accentRed,
              soundrave::iap::product_config::accentGreen,
              soundrave::iap::product_config::accentBlue)),
          purchaseLookAndFeel(accentColour)
    {
#if JUCE_IOS
        setInterceptsMouseClicks(false, true);
#endif
        auto& manager = soundrave::iap::IAPManager::getInstance();
        const bool canManagePurchases = manager.canManagePurchasesHere();

        statusLabel.setFont(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold));
        statusLabel.setJustificationType(juce::Justification::centred);
        statusLabel.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(statusLabel);

        if (canManagePurchases)
        {
            purchaseButton.setButtonText("BUY FULL VERSION");
            purchaseButton.getProperties().set("iapPrimary", true);
            purchaseButton.setLookAndFeel(&purchaseLookAndFeel);
            purchaseButton.setColour(juce::TextButton::buttonColourId,
                                     juce::Colours::transparentBlack);
            purchaseButton.setColour(juce::TextButton::buttonOnColourId,
                                     juce::Colours::transparentBlack);
            purchaseButton.onClick = [] { soundrave::iap::IAPManager::getInstance().purchase(); };
            addAndMakeVisible(purchaseButton);

            restoreButton.setButtonText("RESTORE");
            restoreButton.setLookAndFeel(&purchaseLookAndFeel);
            restoreButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colours::transparentBlack);
            restoreButton.setColour(juce::TextButton::buttonOnColourId,
                                    juce::Colours::transparentBlack);
            restoreButton.onClick = [] { soundrave::iap::IAPManager::getInstance().restore(); };
            addAndMakeVisible(restoreButton);
        }

        manager.addChangeListener(this);
        refresh();
    }

    ~IAPComponent() override
    {
        purchaseButton.setLookAndFeel(nullptr);
        restoreButton.setLookAndFeel(nullptr);
        soundrave::iap::IAPManager::getInstance().removeChangeListener(this);
    }

    std::function<void()> onVisibilityChanged;

    void setProtectedBounds(juce::Rectangle<int> bounds)
    {
        protectedBounds = bounds;
        resized();
    }

    void paint(juce::Graphics& graphics) override
    {
#if JUCE_IOS
        juce::ignoreUnused(graphics);
#else
        const auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient background(juce::Colour::fromRGB(32, 27, 20),
                                        bounds.getX(), bounds.getY(),
                                        juce::Colour::fromRGB(12, 11, 9),
                                        bounds.getX(), bounds.getBottom(), false);
        graphics.setGradientFill(background);
        graphics.fillRect(bounds);
        graphics.setColour(accentColour.withAlpha(0.82f));
        graphics.fillRect(0, 0, getWidth(), 1);
#endif
    }

    void resized() override
    {
#if JUCE_IOS
        const auto outer = getLocalBounds().reduced(8);
        const auto protectedArea = protectedBounds.getIntersection(getLocalBounds());
        auto leftArea = outer.withRight(juce::jmax(outer.getX(), protectedArea.getX() - 6));
        auto rightArea = outer.withLeft(juce::jmin(outer.getRight(), protectedArea.getRight() + 6));
        const auto buttonHeight = juce::jlimit(30, 42, getHeight() / 9);

        auto purchaseArea = leftArea.removeFromBottom(buttonHeight);
        auto statusArea = leftArea.removeFromBottom(juce::jmax(16, buttonHeight / 2));
        auto restoreArea = rightArea.removeFromBottom(buttonHeight);

        purchaseButton.setButtonText(purchaseArea.getWidth() < 112 ? "BUY" : "BUY FULL VERSION");
        purchaseButton.setBounds(purchaseArea);
        restoreButton.setBounds(restoreArea);
        statusLabel.setBounds(statusArea);
        return;
#else
        const bool compact = getWidth() < 400;
        auto area = getLocalBounds().reduced(compact ? 6 : 8, compact ? 3 : 4);
        if (soundrave::iap::IAPManager::getInstance().canManagePurchasesHere())
        {
            auto statusArea = area.removeFromTop(juce::jmax(16, area.getHeight() / 3));
            statusLabel.setBounds(statusArea);

            area.removeFromTop(2);
            auto controls = area;
            const int gap = compact ? 5 : 7;
            const int restoreWidth = juce::jmax(72, controls.getWidth() / 3);
            restoreButton.setBounds(controls.removeFromRight(restoreWidth));
            controls.removeFromRight(gap);
            purchaseButton.setBounds(controls);
            return;
        }

        statusLabel.setBounds(area);
#endif
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        refresh();
    }

    void refresh()
    {
        auto& manager = soundrave::iap::IAPManager::getInstance();
        const bool canManagePurchases = manager.canManagePurchasesHere();

        auto status = manager.getStatusString();
        if (!canManagePurchases && !manager.isAudioAllowed())
            status += " - OPEN STANDALONE APP TO PURCHASE OR RESTORE";

        statusLabel.setText(status, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, accentColour);

        const auto currentState = manager.getState();
        const bool busy = currentState == soundrave::iap::IAPManager::State::Loading
                       || currentState == soundrave::iap::IAPManager::State::Purchasing
                       || currentState == soundrave::iap::IAPManager::State::Restoring;

        const bool canRetryPurchase = currentState == soundrave::iap::IAPManager::State::Failed
                                   && manager.getPrice().isNotEmpty();
        if (canManagePurchases)
        {
            purchaseButton.setEnabled(currentState == soundrave::iap::IAPManager::State::Ready
                                   || canRetryPurchase);
            restoreButton.setEnabled(!busy
                                  && currentState != soundrave::iap::IAPManager::State::Unavailable);
        }
        const bool shouldBeVisible = !manager.isUnlocked();
        if (isVisible() != shouldBeVisible)
        {
            setVisible(shouldBeVisible);
            if (onVisibilityChanged)
                onVisibilityChanged();
        }
    }

    const juce::Colour accentColour;
    PurchaseLookAndFeel purchaseLookAndFeel;
    juce::Label statusLabel;
    juce::TextButton purchaseButton;
    juce::TextButton restoreButton;
    juce::Rectangle<int> protectedBounds;
};
