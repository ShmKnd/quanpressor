#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "QuanpressorDSP.h"
#include "Metering.h"
#include "OutputVuBallistics.h"

class QuanpressorAudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    QuanpressorAudioProcessor();
    ~QuanpressorAudioProcessor() override { cancelPendingUpdate(); }
    void prepareToPlay(double,int) override; void releaseResources() override {}
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Quanpressor"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; } double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override; void setStateInformation(const void*,int) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    juce::AudioProcessorValueTreeState apvts;
    QuanpressorMeters meters;
    void requestOptoReset() noexcept { dsp.requestPhotoCellReset(); }
private:
    void handleAsyncUpdate() override;

    QuanpressorDSP dsp;
    int historyCounter = 0;
    OutputVuBallistics outputVu;
    float graphDetectorDb = -100.0f;
    float inputPeakBallisticDb = -100.0f;
    float outputPeakBallisticDb = -100.0f;
    float inputRmsBallisticDb = -100.0f;
    float outputRmsBallisticDb = -100.0f;
    std::array<float, 4> grMatchAverageDb {};
    QuanpressorDSP::Mode previousGrMatchMode = QuanpressorDSP::FET;
    float grMatchTargetDb = 0.0f;
    float grMatchThresholdDb = -18.0f;
    float grMatchElapsedSeconds = 0.0f;
    float grMatchInputLevelDb = -100.0f;
    double grMatchWindowGrSeconds = 0.0;
    float grMatchWindowSeconds = 0.0f;
    int grMatchStableWindows = 0;
    bool grMatchActive = false;
    std::atomic<float> pendingThresholdDb { -18.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuanpressorAudioProcessor)
};
