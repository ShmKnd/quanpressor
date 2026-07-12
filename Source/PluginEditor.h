#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"

class QuanpressorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QuanpressorAudioProcessorEditor(QuanpressorAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

public:
    struct Knob : juce::Slider
    {
        enum class Colourway { Green, Yellow, Red, Blue, Grey };

        bool useFetRatioGrid = false;
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

            static constexpr double ratios[] = { 4.0, 8.0, 12.0, 20.0 };
            double nearest = ratios[0];
            double nearestDistance = std::abs(attemptedValue - nearest);

            for (double ratioValue : ratios)
            {
                const double distance = std::abs(attemptedValue - ratioValue);
                if (distance < nearestDistance)
                {
                    nearest = ratioValue;
                    nearestDistance = distance;
                }
            }

            return nearest;
        }

        void paint(juce::Graphics&) override;
    };

    struct ImageButton : juce::Button
    {
        enum class Kind { Fet, Opto, Vca, VariMu, GrMatch, AutoRelease, AutoGain, StChain, ScFilter, ResetPhotocell };

        explicit ImageButton(const juce::String& name) : juce::Button(name) {}
        Kind kind = Kind::Fet;
        void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    };

private:
    struct Meter : juce::Component
    {
        std::atomic<float>* value{};
        std::atomic<float>* rmsValue{};
        bool gr = false;
        juce::String title;
        void paint(juce::Graphics&) override;
    };

    struct VuMeter : juce::Component
    {
        std::atomic<float>* value{};
        void paint(juce::Graphics&) override;
    };

    struct Graph : juce::Component
    {
        QuanpressorAudioProcessor& p;
        explicit Graph(QuanpressorAudioProcessor& x) : p(x) {}
        void paint(juce::Graphics&) override;
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

    struct NoMeterCurtain : juce::Component
    {
        NoMeterCurtain() { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics&) override;
    };

    void timerCallback() override;
    void updateArchitectureControls();
    void setModeFromButton(int index);
    void addKnob(Knob&, const char*);

    QuanpressorAudioProcessor& proc;
    juce::Component content;
    juce::ComboBox mode;
    std::array<ImageButton, 4> modeButtons {
        ImageButton { "FET" },
        ImageButton { "OPTO" },
        ImageButton { "VCA" },
        ImageButton { "Vari-Mu" }
    };
    ImageButton autoLevel { "AUTO GAIN" };
    ImageButton autoRelease { "AUTO RELEASE" };
    ImageButton grMatch { "GR MATCH" };
    ImageButton sidechainHpf { "SC FILTER" };
    ImageButton stereoChain { "ST CHAIN" };
    ImageButton optoReset { "RESET PHOTOCELL" };

    Knob input, threshold, ratio, drive, attack, release, knee, makeup;
    Knob highFreq, highAmt, lowFreq, lowAmt, output, mix, timeConstant;

    Meter inMeter, outMeter, grMeter;
    VuMeter vuMeter;
    Graph graph;
    HistoryGraph historyGraph;
    EmbosOverlay grCurveEmbos;
    EmbosOverlay grHistoryEmbos;
    NoMeterCurtain noMeterCurtain;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sa;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> ca;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> ba;
};
