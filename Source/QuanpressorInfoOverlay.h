#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

class QuanpressorInfoOverlay final : public juce::Component
{
public:
    struct Info
    {
        juce::String product;
        juce::String version;
        juce::String licenseId;
        juce::String user;
        juce::String status;
    };

    using CloseCallback = std::function<void()>;

    QuanpressorInfoOverlay(const Info& info, const juce::String& libraryText,
                          CloseCallback closeCallbackIn = {})
        : closeCallback(std::move(closeCallbackIn))
    {
        setInterceptsMouseClicks(true, true);

        addAndMakeVisible(tabs);
        tabs.setTabBarDepth(36);
        tabs.setColour(juce::TabbedComponent::backgroundColourId,
                       juce::Colours::transparentBlack);

        auto& tabBar = tabs.getTabbedButtonBar();
        tabBar.setColour(juce::TabbedButtonBar::tabTextColourId,
                         juce::Colour(0xffaeb3b8));
        tabBar.setColour(juce::TabbedButtonBar::frontTextColourId, accent);
        tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
        tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId,
                         juce::Colour(0xff555a60));

        tabs.addTab("Info", juce::Colours::transparentBlack,
                    new DetailsComponent(info, accent), true);
        tabs.addTab("Library Licenses", juce::Colours::transparentBlack,
                    new LibrariesComponent(libraryText, accent), true);

        tabUnderline = std::make_unique<TabUnderlineOverlay>(tabs, accent);
        tabBar.addAndMakeVisible(*tabUnderline);
        tabUnderline->toFront(true);

        closeButton.onClick = [this]
        {
            if (closeCallback)
                closeCallback();
        };
        closeButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(closeButton);
        closeButton.toFront(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xa6000000));

        const auto panel = getPanelBounds().toFloat();
        g.setColour(juce::Colour(0xff25282c));
        g.fillRoundedRectangle(panel, 8.0f);
        g.setColour(juce::Colour(0xff555a60));
        g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        const auto panel = getPanelBounds();
        constexpr int closeSize = 28;
        closeButton.setBounds(panel.getRight() - closeSize - 18,
                              panel.getY() + 8,
                              closeSize,
                              closeSize);
        tabs.setBounds(panel.reduced(12));
        if (tabUnderline)
            tabUnderline->setBounds(tabs.getTabbedButtonBar().getLocalBounds());
    }

private:
    static constexpr juce::uint32 accentArgb = 0xffd9a928;
    const juce::Colour accent { accentArgb };

    juce::Rectangle<int> getPanelBounds() const
    {
        return getLocalBounds().reduced(48, 36);
    }

    class TabUnderlineOverlay final : public juce::Component,
                                      private juce::Timer
    {
    public:
        TabUnderlineOverlay(juce::TabbedComponent& tabsIn, juce::Colour accentIn)
            : tabs(tabsIn),
              tabBar(tabsIn.getTabbedButtonBar()),
              underlineColour(accentIn),
              lastIndex(tabsIn.getCurrentTabIndex())
        {
            setInterceptsMouseClicks(false, false);
            startTimerHz(15);
        }

        ~TabUnderlineOverlay() override
        {
            stopTimer();
        }

        void paint(juce::Graphics& g) override
        {
            const int activeIndex = getActiveIndex();
            if (activeIndex < 0)
                return;

            const auto buttons = getTabButtons();
            juce::Rectangle<int> tabBounds;
            if (activeIndex < buttons.size())
            {
                tabBounds = buttons.getUnchecked(activeIndex)->getBounds();
            }
            else
            {
                const int tabCount = tabs.getNumTabs();
                if (tabCount <= 0)
                    return;

                const int tabWidth = tabBar.getWidth() / tabCount;
                tabBounds = { activeIndex * tabWidth, 0, tabWidth, tabBar.getHeight() };
            }

            constexpr float padding = 6.0f;
            constexpr float thickness = 4.0f;
            const juce::Rectangle<float> line(
                static_cast<float>(tabBounds.getX()) + padding,
                static_cast<float>(getHeight()) - thickness - 1.0f,
                static_cast<float>(juce::jmax(0, tabBounds.getWidth()
                                                  - juce::roundToInt(padding * 2.0f))),
                thickness);
            g.setColour(underlineColour);
            g.fillRoundedRectangle(line, 1.5f);
        }

    private:
        juce::Array<juce::Button*> getTabButtons() const
        {
            juce::Array<juce::Button*> buttons;
            for (int index = 0; index < tabBar.getNumChildComponents(); ++index)
                if (auto* button = dynamic_cast<juce::Button*>(
                        tabBar.getChildComponent(index)))
                    buttons.add(button);
            return buttons;
        }

        int getActiveIndex() const
        {
            const auto buttons = getTabButtons();
            for (int index = 0; index < buttons.size(); ++index)
                if (buttons.getUnchecked(index)->getToggleState())
                    return index;
            return tabs.getCurrentTabIndex();
        }

        void timerCallback() override
        {
            const int activeIndex = getActiveIndex();
            if (activeIndex != lastIndex)
            {
                lastIndex = activeIndex;
                repaint();
            }
        }

        juce::TabbedComponent& tabs;
        juce::TabbedButtonBar& tabBar;
        juce::Colour underlineColour;
        int lastIndex = -1;
    };

    class CloseButton final : public juce::Button
    {
    public:
        CloseButton() : juce::Button("Close info") {}

        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
        {
            const auto area = getLocalBounds().toFloat().reduced(6.0f);
            juce::Path path;
            path.startNewSubPath(area.getTopLeft());
            path.lineTo(area.getBottomRight());
            path.startNewSubPath(area.getTopRight());
            path.lineTo(area.getBottomLeft());
            path.applyTransform(juce::AffineTransform::translation(5.0f, 0.0f));

            const auto colour = isButtonDown
                ? juce::Colours::red
                : isMouseOver
                    ? juce::Colour(accentArgb)
                    : juce::Colour(0xffe4e5e6);
            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(1.6f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    };

    class DetailsComponent final : public juce::Component
    {
    public:
        DetailsComponent(const Info& info, juce::Colour accentIn)
            : product(info.product),
              version(info.version),
              licenseId(info.licenseId),
              user(info.user),
              status(info.status),
              websiteLink("https://plugin.soundrave.org",
                          juce::URL("https://plugin.soundrave.org")),
              accentColour(accentIn)
        {
            websiteLink.setJustificationType(juce::Justification::centredLeft);
            websiteLink.setFont(juce::Font(juce::FontOptions(13.0f)), false);
            websiteLink.setColour(juce::HyperlinkButton::textColourId, accentColour);
            websiteLink.setMouseCursor(juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible(websiteLink);
        }

        void paint(juce::Graphics& g) override
        {
            g.setColour(accentColour);
            g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
            g.drawText(product, productBounds, juce::Justification::centredLeft);

            g.setColour(juce::Colour(0xffe4e5e6));
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText("Copyright 2026 Sound Rave", copyrightBounds,
                       juce::Justification::centredLeft);
            g.drawText("Version : " + version, versionBounds,
                       juce::Justification::centredLeft);
            g.drawText("Build : " + juce::String(__DATE__) + " "
                           + juce::String(__TIME__),
                       buildBounds, juce::Justification::centredLeft);
            g.drawText("Website :", websiteLabelBounds,
                       juce::Justification::centredLeft);
            g.drawText("License Status : " + fallback(status), statusBounds,
                       juce::Justification::centredLeft);
            g.drawText("Serial Nr : " + fallback(licenseId), licenseBounds,
                       juce::Justification::centredLeft);
            g.drawText("User : " + fallback(user), userBounds,
                       juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(18);
            productBounds = area.removeFromTop(34);
            area.removeFromTop(6);

            copyrightBounds = takeLine(area);
            versionBounds = takeLine(area);
            buildBounds = takeLine(area);

            auto websiteRow = area.removeFromTop(22);
            const juce::Font bodyFont(juce::FontOptions(13.0f));
            const int labelWidth = juce::GlyphArrangement::getStringWidthInt(
                bodyFont, "Website :") + 4;
            const int linkWidth = juce::GlyphArrangement::getStringWidthInt(
                bodyFont, "https://plugin.soundrave.org") + 6;
            websiteLabelBounds = websiteRow.removeFromLeft(labelWidth);
            websiteLink.setBounds(websiteRow.removeFromLeft(linkWidth));

            area.removeFromTop(10);
            statusBounds = takeLine(area);
            licenseBounds = takeLine(area);
            userBounds = takeLine(area);
        }

    private:
        static juce::Rectangle<int> takeLine(juce::Rectangle<int>& area)
        {
            auto line = area.removeFromTop(22);
            area.removeFromTop(2);
            return line;
        }

        static juce::String fallback(const juce::String& value)
        {
            return value.isNotEmpty() ? value : "N/A";
        }

        juce::String product;
        juce::String version;
        juce::String licenseId;
        juce::String user;
        juce::String status;
        juce::HyperlinkButton websiteLink;
        juce::Colour accentColour;
        juce::Rectangle<int> productBounds;
        juce::Rectangle<int> copyrightBounds;
        juce::Rectangle<int> versionBounds;
        juce::Rectangle<int> buildBounds;
        juce::Rectangle<int> websiteLabelBounds;
        juce::Rectangle<int> statusBounds;
        juce::Rectangle<int> licenseBounds;
        juce::Rectangle<int> userBounds;
    };

    class LibrariesComponent final : public juce::Component
    {
    public:
        LibrariesComponent(const juce::String& text, juce::Colour accentIn)
        {
            editor.setMultiLine(true);
            editor.setReadOnly(true);
            editor.setScrollbarsShown(true);
            editor.setCaretVisible(false);
            editor.setPopupMenuEnabled(true);
            editor.setFont(juce::Font(juce::FontOptions(
                juce::Font::getDefaultMonospacedFontName(), 13.0f,
                juce::Font::plain)));
            editor.setColour(juce::TextEditor::backgroundColourId,
                             juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::textColourId,
                             juce::Colour(0xffd7dadd));
            editor.setColour(juce::TextEditor::outlineColourId,
                             juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::focusedOutlineColourId,
                             juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::highlightColourId,
                             accentIn.withAlpha(0.22f));
            editor.setText(text, false);

            lookAndFeel.setColour(juce::ScrollBar::thumbColourId, accentIn);
            lookAndFeel.setColour(juce::ScrollBar::backgroundColourId,
                                  juce::Colour(0xff25282c));
            editor.setLookAndFeel(&lookAndFeel);
            addAndMakeVisible(editor);
        }

        ~LibrariesComponent() override
        {
            editor.setLookAndFeel(nullptr);
        }

        void resized() override
        {
            editor.setBounds(getLocalBounds().reduced(8));
        }

    private:
        juce::TextEditor editor;
        juce::LookAndFeel_V4 lookAndFeel;
    };

    CloseCallback closeCallback;
    CloseButton closeButton;
    std::unique_ptr<TabUnderlineOverlay> tabUnderline;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuanpressorInfoOverlay)
};
