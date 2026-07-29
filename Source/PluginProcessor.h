#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "QuanpressorDSP.h"
#include "QuanpressorFactoryPresets.h"
#include "Metering.h"
#include "OutputVuBallistics.h"

class GrMatchThresholdOwnershipTestAccess;

class QuanpressorAudioProcessor : public juce::AudioProcessor,
                                  private juce::AsyncUpdater,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    QuanpressorAudioProcessor();
    ~QuanpressorAudioProcessor() override;
    void prepareToPlay(double,int) override; void releaseResources() override {}
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Quanpressor"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; } double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return quanpressorFactoryPresetCount; }
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override; void setStateInformation(const void*,int) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void loadFactoryPreset(int index);
    juce::StringArray getUserPresetNames() const;
    juce::String saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);
    bool deleteUserPreset(const juce::String& name);
    juce::String getCurrentPresetName() const;
    juce::File getUserPresetDirectory() const;
    juce::AudioProcessorValueTreeState apvts;
    QuanpressorMeters meters;
    void requestOptoReset() noexcept { dsp.requestPhotoCellReset(); }
private:
    friend class GrMatchThresholdOwnershipTestAccess;
    void handleAsyncUpdate() override;
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    juce::File getUserPresetFile(const juce::String& name) const;
    static juce::String makeSafePresetName(const juce::String& name);

    QuanpressorDSP dsp;
    int historyCounter = 0;
    std::array<OutputVuBallistics, 2> inputVu;
    std::array<OutputVuBallistics, 2> outputVu;
    float graphDetectorDb = -100.0f;
    std::array<float, 2> inputPeakBallisticDb {{ -100.0f, -100.0f }};
    std::array<float, 2> outputPeakBallisticDb {{ -100.0f, -100.0f }};
    std::array<float, 2> inputRmsBallisticDb {{ -100.0f, -100.0f }};
    std::array<float, 2> outputRmsBallisticDb {{ -100.0f, -100.0f }};
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
    std::atomic<float> processorThresholdExpectedDb { -18.0f };
    std::atomic<float> lastExternalThresholdDb { -18.0f };
    std::atomic<bool> thresholdUpdatePending { false };
    std::atomic<bool> processorThresholdWriteActive { false };
    std::atomic<bool> grMatchCancellationPending { false };
    std::atomic<bool> restoringState { false };
    std::atomic<int> currentProgramIndex { 0 };
    mutable juce::CriticalSection presetStateLock;
    juce::String currentPresetName { quanpressorFactoryPresets[0].name };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuanpressorAudioProcessor)
};
