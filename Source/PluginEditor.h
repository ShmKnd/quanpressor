#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"
#include "QuanpressorInfoOverlay.h"
#include "license/LicenseComponent.h"

class MeterPrivacyRenderer;

class QuanpressorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QuanpressorAudioProcessorEditor(QuanpressorAudioProcessor&);
    ~QuanpressorAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

public:
    struct Knob : juce::Slider
    {
        enum class Colourway { Green, Yellow, Red, Blue, Grey };

        bool useFetRatioGrid = false;
        bool useFetAttackDisplay = false;
        bool useIntegerGrid = false;
        bool useTimeConstantScale = false;
        juce::String parameterId;
        Colourway colourway = Colourway::Green;

        Knob()
        {
            setSliderStyle(RotaryHorizontalVerticalDrag);
            setTextBoxStyle(NoTextBox, false, 0, 0);
        }

        double snapValue(double attemptedValue, DragMode dragMode) override
        {
            if (useIntegerGrid)
                return std::round(attemptedValue);

            if (!useFetRatioGrid)
                return juce::Slider::snapValue(attemptedValue, dragMode);

            return QuanpressorDSP::fetRatioValue(static_cast<float>(attemptedValue));
        }

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

    private:
        void movePopupCloserToKnob();
    };

    struct ImageButton : juce::Button
    {
        enum class Kind
        {
            Fet, Opto, Vca, VariMu, GrMatch, AutoRelease, AutoGain, StChain, ScFilter, ResetPhotocell,
            VuOutput, VuInput, VuGr, NoMeter, Os2, Os4, ExternalSidechain
        };

        explicit ImageButton(const juce::String& name) : juce::Button(name) {}
        Kind kind = Kind::Fet;
        void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    };

    struct InfoButton : juce::Button
    {
        InfoButton() : juce::Button("Info") {}
        void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
    };

private:
    struct TooltipLookAndFeel : juce::LookAndFeel_V4
    {
        TooltipLookAndFeel()
        {
            setColour(juce::TooltipWindow::textColourId, juce::Colour::fromRGB(236, 236, 232));
            setColour(juce::BubbleComponent::backgroundColourId,
                      juce::Colour::fromRGB(24, 25, 27).withAlpha(0.98f));
            setColour(juce::BubbleComponent::outlineColourId, juce::Colour::fromRGB(104, 108, 112));
        }

        juce::Rectangle<int> getTooltipBounds(const juce::String&, juce::Point<int> screenPos,
                                              juce::Rectangle<int> parentArea) override;
        void drawTooltip(juce::Graphics&, const juce::String&, int width, int height) override;
        void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                        const juce::Rectangle<float>& body) override;
    };

    struct PresetLookAndFeel : juce::LookAndFeel_V4
    {
        PresetLookAndFeel();
        void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonWidth, int buttonHeight,
                          juce::ComboBox&) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    };

    struct PresetArrowButton : juce::Button
    {
        explicit PresetArrowButton(bool pointsRightIn)
            : juce::Button(pointsRightIn ? "Next preset" : "Previous preset"),
              pointsRight(pointsRightIn) {}

        void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
        bool pointsRight = false;
    };

    struct Meter : juce::Component
    {
        std::atomic<float>* value{};
        std::array<std::atomic<float>*, 2> peakValues {};
        std::array<std::atomic<float>*, 2> rmsValues {};
        std::array<float, 2> heldPeakDb {{ -100.0f, -100.0f }};
        std::array<double, 2> holdUntilMs {};
        double lastHoldUpdateMs = 0.0;
        bool gr = false;
        juce::String title;
        void paint(juce::Graphics&) override;
    };

    struct VuMeter : juce::Component
    {
        enum class Mode { output = 0, input = 1, gainReduction = 2 };

        std::atomic<float>* outputValue{};
        std::atomic<float>* inputValue{};
        std::atomic<float>* grValue{};
        Mode mode = Mode::output;
        void paint(juce::Graphics&) override;
    };

    struct Graph : juce::Component
    {
        QuanpressorAudioProcessor& p;
        explicit Graph(QuanpressorAudioProcessor& x) : p(x) {}
        void paint(juce::Graphics&) override;

        float displayedInputDb = -48.0f;
        double lastDisplayUpdateMs = 0.0;
        bool displayStateInitialised = false;
    };

    struct HistoryGraph : juce::Component
    {
        QuanpressorAudioProcessor& p;
        explicit HistoryGraph(QuanpressorAudioProcessor& x) : p(x) {}
        void paint(juce::Graphics&) override;
    };

    struct EmbosOverlay : juce::Component
    {
        enum class Kind { GrCurve, GrHistory };
        Kind kind = Kind::GrHistory;
        void paint(juce::Graphics&) override;
    };

    struct GrMatchingOverlay : juce::Component
    {
        GrMatchingOverlay() { setInterceptsMouseClicks(false, false); }
        std::atomic<bool>* matching{};
        void paint(juce::Graphics&) override;
    };

    void timerCallback() override;
    void updateArchitectureControls();
    void setModeFromButton(int index);
    void setVuModeFromButton(int index);
    void setOversamplingFromButton(int factor);
    void refreshPresetMenu();
    void loadPresetMenuItem(int menuId);
    void selectRelativePreset(int direction);
    void showSavePresetDialog();
    void saveUserPresetFromDialog(const juce::String& name);
    void deleteSelectedUserPreset();
    void updatePresetDisplay();
    void showInfoDialog();
    void closeInfoPanel();
    void updateLicenseLayout();
    void updateMeterPrivacy(bool enabled, bool forceCapture = false);
    void addKnob(Knob&, const char*);

    QuanpressorAudioProcessor& proc;
    TooltipLookAndFeel tooltipLookAndFeel;
    PresetLookAndFeel presetLookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 1700 };
    juce::Component content;
    LicenseComponent licenseComponent;
    juce::ComboBox presetMenu;
    PresetArrowButton previousPreset { false };
    PresetArrowButton nextPreset { true };
    juce::StringArray userPresetNames;
    juce::String displayedPresetName;
    int selectedPresetMenuId = 1;
    InfoButton infoButton;
    std::unique_ptr<QuanpressorInfoOverlay> infoPanel;
    juce::ComboBox mode;
    std::array<ImageButton, 4> modeButtons {
        ImageButton { "FET" },
        ImageButton { "OPTO" },
        ImageButton { "VCA" },
        ImageButton { "Vari-Mu" }
    };
    std::array<ImageButton, 3> vuModeButtons {
        ImageButton { "VU OUTPUT" },
        ImageButton { "VU INPUT" },
        ImageButton { "VU GR" }
    };
    ImageButton autoLevel { "AUTO GAIN" };
    ImageButton autoRelease { "AUTO RELEASE" };
    ImageButton grMatch { "GR MATCH" };
    ImageButton externalSidechain { "EXT SC" };
    ImageButton sidechainHpf { "SC FILTER" };
    ImageButton stereoChain { "ST CHAIN" };
    ImageButton optoReset { "RESET PHOTOCELL" };
    ImageButton noMeter { "NO METER" };
    ImageButton oversampling2x { "OS x2" };
    ImageButton oversampling4x { "OS x4" };

    Knob input, threshold, ratio, drive, attack, release, knee, makeup;
    Knob highFreq, highAmt, lowFreq, lowAmt, output, mix, timeConstant;

    Meter inMeter, outMeter, grMeter;
    std::array<VuMeter, 2> vuMeters;
    Graph graph;
    HistoryGraph historyGraph;
    EmbosOverlay grCurveEmbos;
    EmbosOverlay grHistoryEmbos;
    GrMatchingOverlay grMatchingOverlay;
    std::array<std::unique_ptr<MeterPrivacyRenderer>, 2> meterPrivacyRenderers;
    bool meterPrivacyEnabled = false;
    int meterPrivacyCaptureCounter = 0;
    int displayedArchitecture = -1;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sa;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> ca;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> ba;
};
