#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct MeterChannelStats
    {
        float peak = 0.0f;
        double energy = 0.0;
    };

    std::array<MeterChannelStats, 2> calculateStereoMeterStats(const juce::AudioBuffer<float>& buffer)
    {
        std::array<MeterChannelStats, 2> stats {};
        const int samples = buffer.getNumSamples();
        const int channelsToScan = juce::jmin(buffer.getNumChannels(), 2);

        for (int ch = 0; ch < channelsToScan; ++ch)
        {
            auto& channelStats = stats[static_cast<size_t>(ch)];
            const auto* data = buffer.getReadPointer(ch);

            for (int i = 0; i < samples; ++i)
            {
                const float sample = data[i];
                channelStats.peak = juce::jmax(channelStats.peak, std::abs(sample));
                channelStats.energy += static_cast<double>(sample) * static_cast<double>(sample);
            }
        }

        if (buffer.getNumChannels() == 1)
            stats[1] = stats[0];

        return stats;
    }

    void updateMeterBallistic(float targetDb, float& stateDb, std::atomic<float>& destination,
                              int samples, double sampleRate,
                              float attackSeconds, float releaseSeconds)
    {
        const float timeSeconds = targetDb > stateDb ? attackSeconds : releaseSeconds;
        const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                          / static_cast<float>(sampleRate * timeSeconds));
        stateDb += alpha * (targetDb - stateDb);
        destination.store(stateDb, std::memory_order_relaxed);
    }

    float optoThresholdForTargetGr(float inputPeakDb, float targetGrDb, float ratio)
    {
        const float inputLinear = juce::Decibels::decibelsToGain(inputPeakDb);
        const float targetGain = juce::Decibels::decibelsToGain(juce::jmin(0.0f, targetGrDb));
        const float compressionFactor = juce::jmap(juce::jlimit(1.5f, 20.0f, ratio),
                                                   1.5f, 20.0f, 5.0f, 20.0f);
        const float sensitivity = 0.5f + juce::jlimit(0.0f, 1.0f, 0.24f + ratio / 28.0f) * 4.0f;
        const float requiredAttenuation = juce::jlimit(0.0f, 1.0f,
            (1.0f / juce::jmax(0.001f, targetGain) - 1.0f) / compressionFactor);
        const float thresholdLinear = juce::jmax(juce::Decibels::decibelsToGain(-48.0f),
            inputLinear - requiredAttenuation / sensitivity);
        return juce::jlimit(-48.0f, 0.0f,
            juce::Decibels::gainToDecibels(thresholdLinear, -48.0f));
    }
}

QuanpressorAudioProcessor::QuanpressorAudioProcessor()
: juce::AudioProcessor(juce::AudioProcessor::BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"PARAMS",makeLayout())
{
    apvts.addParameterListener("threshold", this);
}

QuanpressorAudioProcessor::~QuanpressorAudioProcessor()
{
    apvts.removeParameterListener("threshold", this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout QuanpressorAudioProcessor::makeLayout()
{
    using namespace juce; std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back(std::make_unique<AudioParameterChoice>("mode","Mode",StringArray{"FET","OPTO","VCA","VARI-MU"},0));
    auto add=[&](const char* id,const char* name,float a,float b,float step,float def,const char* suffix="",float displayScale=1.0f){ p.push_back(std::make_unique<AudioParameterFloat>(id,name,NormalisableRange<float>(a,b,step),def,String(),AudioProcessorParameter::genericParameter,[suffix,displayScale](float v,int){return String(v*displayScale,1)+suffix;},nullptr));};
    add("input","Input",-24,24,0.1f,0," dB"); add("threshold","Threshold",-48,0,0.1f,-18," dB"); add("ratio","Ratio",1.5f,20.0f,0.1f,4,":1"); add("drive","Drive",0,18,0.1f,0," dB");
    add("attack","Attack",0.02f,100,0.01f,10," ms"); add("release","Release",20,1200,1,150," ms"); add("knee","Knee",0,18,0.1f,6," dB");
    add("makeup","Makeup",-12,24,0.1f,0," dB"); add("highFreq","High Preserve Frequency",1000,18000,1,8000," Hz"); add("highAmt","High Preserve Amount",0,1,0.001f,0," %",100.0f);
    add("lowFreq","Low Preserve Frequency",30,600,1,120," Hz"); add("lowAmt","Low Preserve Amount",0,1,0.001f,0," %",100.0f); add("output","Output",-24,24,0.1f,0," dB");
    add("mix","Mix",0,100,0.1f,100," %");
    p.push_back(std::make_unique<AudioParameterChoice>("muSpeed","Time Constant",StringArray{"1","2","3","4","5","6"},2));
    p.push_back(std::make_unique<AudioParameterChoice>("vuMode","VU Mode",StringArray{"OUTPUT","INPUT","GR"},0));
    p.push_back(std::make_unique<AudioParameterBool>("autoLevel","Auto Level Match",false));
    p.push_back(std::make_unique<AudioParameterBool>("autoRelease","Auto Release",false));
    p.push_back(std::make_unique<AudioParameterBool>("grMatch","GR Match",false));
    p.push_back(std::make_unique<AudioParameterBool>("noMeter","No Meter",false));
    p.push_back(std::make_unique<AudioParameterBool>("sidechainHpf","Sidechain HPF",false));
    p.push_back(std::make_unique<AudioParameterBool>("stereoChain","Stereo Chain",true));
    p.push_back(std::make_unique<AudioParameterBool>("oversampling","Oversampling",false));
    p.push_back(std::make_unique<AudioParameterChoice>("oversamplingFactor","Oversampling Factor",
                                                       StringArray{"2x","4x"},0));
    return {p.begin(),p.end()};
}
void QuanpressorAudioProcessor::prepareToPlay(double sr,int bs)
{
    dsp.prepare(sr,bs,getTotalNumOutputChannels());
    setLatencySamples(dsp.getLatencySamples());
    for (auto& meter : inputVu)
        meter.prepare(sr);
    for (auto& meter : outputVu)
        meter.prepare(sr);
    graphDetectorDb = -100.0f;
    inputPeakBallisticDb.fill(-100.0f);
    outputPeakBallisticDb.fill(-100.0f);
    inputRmsBallisticDb.fill(-100.0f);
    outputRmsBallisticDb.fill(-100.0f);
    grMatchAverageDb.fill(0.0f);
    previousGrMatchMode = static_cast<QuanpressorDSP::Mode>(
        static_cast<int>(*apvts.getRawParameterValue("mode")));
    grMatchThresholdDb = *apvts.getRawParameterValue("threshold");
    pendingThresholdDb.store(grMatchThresholdDb);
    processorThresholdExpectedDb.store(grMatchThresholdDb);
    lastExternalThresholdDb.store(grMatchThresholdDb);
    thresholdUpdatePending.store(false);
    processorThresholdWriteActive.store(false);
    grMatchCancellationPending.store(false);
    grMatchInputLevelDb = -100.0f;
    grMatchWindowGrSeconds = 0.0;
    grMatchWindowSeconds = 0.0f;
    grMatchStableWindows = 0;
    grMatchActive = false;
    meters.grMatching.store(false);
}
bool QuanpressorAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& l) const { return l.getMainInputChannelSet()==l.getMainOutputChannelSet() && (l.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()); }
void QuanpressorAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int samples = b.getNumSamples();

    const auto rawInputStats = calculateStereoMeterStats(b);

    QuanpressorDSP::Controls c;
    c.mode = static_cast<QuanpressorDSP::Mode>(static_cast<int>(*apvts.getRawParameterValue("mode")));
    c.inputDb = *apvts.getRawParameterValue("input");
    c.thresholdDb = *apvts.getRawParameterValue("threshold");
    c.ratio = *apvts.getRawParameterValue("ratio");
    c.driveDb = *apvts.getRawParameterValue("drive");
    c.attackMs = *apvts.getRawParameterValue("attack");
    c.releaseMs = *apvts.getRawParameterValue("release");
    c.kneeDb = *apvts.getRawParameterValue("knee");
    c.makeupDb = *apvts.getRawParameterValue("makeup");
    c.highBleedHz = *apvts.getRawParameterValue("highFreq");
    c.highBleedAmt = *apvts.getRawParameterValue("highAmt");
    c.lowBleedHz = *apvts.getRawParameterValue("lowFreq");
    c.lowBleedAmt = *apvts.getRawParameterValue("lowAmt");
    c.outputDb = *apvts.getRawParameterValue("output");
    c.mix = *apvts.getRawParameterValue("mix") * 0.01f;
    c.varMuSpeed = static_cast<int>(*apvts.getRawParameterValue("muSpeed"));
    c.autoLevel = *apvts.getRawParameterValue("autoLevel") > 0.5f;
    c.autoRelease = *apvts.getRawParameterValue("autoRelease") > 0.5f;
    c.grMatch = *apvts.getRawParameterValue("grMatch") > 0.5f;
    c.sidechainHpf = *apvts.getRawParameterValue("sidechainHpf") > 0.5f;
    c.stereoChain = *apvts.getRawParameterValue("stereoChain") > 0.5f;
    c.oversampling = *apvts.getRawParameterValue("oversampling") > 0.5f;
    c.oversamplingFactor = *apvts.getRawParameterValue("oversamplingFactor") > 0.5f ? 4 : 2;

    const float inputGainLinear = juce::Decibels::decibelsToGain(c.inputDb);
    // Each INPUT VU follows its channel after INPUT GAIN. Mono buses duplicate
    // channel zero above so the two needles behave as a dual-mono meter pair.
    const double inputGainPower = static_cast<double>(inputGainLinear)
                                * static_cast<double>(inputGainLinear);
    for (size_t ch = 0; ch < inputVu.size(); ++ch)
    {
        const double meanSquare = rawInputStats[ch].energy * inputGainPower
                                / static_cast<double>(juce::jmax(1, samples));
        meters.inputVuDb[ch].store(inputVu[ch].process(meanSquare, samples),
                                   std::memory_order_relaxed);
    }
    std::array<float, 2> inputPeakTargetDb {};
    for (size_t ch = 0; ch < inputPeakTargetDb.size(); ++ch)
    {
        inputPeakTargetDb[ch] = juce::Decibels::gainToDecibels(rawInputStats[ch].peak * inputGainLinear, -100.0f);
        updateMeterBallistic(inputPeakTargetDb[ch], inputPeakBallisticDb[ch], meters.inputDb[ch],
                             samples, getSampleRate(), 0.008f, 0.140f);
    }
    const float linkedInputPeakTargetDb = juce::jmax(inputPeakTargetDb[0], inputPeakTargetDb[1]);
    {
        if (grMatchInputLevelDb <= -99.0f && linkedInputPeakTargetDb > -99.0f)
            grMatchInputLevelDb = linkedInputPeakTargetDb;
        else
        {
            const float timeSeconds = linkedInputPeakTargetDb > grMatchInputLevelDb ? 0.20f : 1.00f;
            const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                              / static_cast<float>(getSampleRate() * timeSeconds));
            grMatchInputLevelDb += alpha * (linkedInputPeakTargetDb - grMatchInputLevelDb);
        }
    }
    for (size_t ch = 0; ch < inputRmsBallisticDb.size(); ++ch)
    {
        const float channelRms = static_cast<float>(std::sqrt(rawInputStats[ch].energy
                                                              / juce::jmax(1, samples)));
        const float targetDb = juce::Decibels::gainToDecibels(channelRms * inputGainLinear, -100.0f);
        updateMeterBallistic(targetDb, inputRmsBallisticDb[ch], meters.inputRmsDb[ch],
                             samples, getSampleRate(), 0.300f, 0.650f);
    }

    const bool grMatchEnabled = c.grMatch
                             && !grMatchCancellationPending.load(std::memory_order_acquire);
    const auto requestedMode = c.mode;

    if (grMatchEnabled && requestedMode != previousGrMatchMode)
    {
        grMatchTargetDb = grMatchAverageDb[(size_t) previousGrMatchMode];
        grMatchThresholdDb = requestedMode == QuanpressorDSP::OPTO && grMatchInputLevelDb > -80.0f
            ? optoThresholdForTargetGr(grMatchInputLevelDb, grMatchTargetDb, c.ratio)
            : c.thresholdDb;
        grMatchElapsedSeconds = 0.0f;
        grMatchWindowGrSeconds = 0.0;
        grMatchWindowSeconds = 0.0f;
        grMatchStableWindows = 0;
        grMatchActive = true;

        if (requestedMode == QuanpressorDSP::OPTO)
        {
            c.thresholdDb = grMatchThresholdDb;
            pendingThresholdDb.store(grMatchThresholdDb, std::memory_order_relaxed);
            thresholdUpdatePending.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }
    else if (!grMatchEnabled)
    {
        grMatchActive = false;
    }

    // GR Match is managed here by moving the public Threshold parameter. The DSP
    // receives no hidden matching offset.
    c.grMatch = false;
    dsp.process(b, c);

    const auto outputStats = calculateStereoMeterStats(b);

    for (size_t ch = 0; ch < outputPeakBallisticDb.size(); ++ch)
    {
        const float targetDb = juce::Decibels::gainToDecibels(outputStats[ch].peak, -100.0f);
        updateMeterBallistic(targetDb, outputPeakBallisticDb[ch], meters.outputDb[ch],
                             samples, getSampleRate(), 0.008f, 0.140f);
    }
    for (size_t ch = 0; ch < outputRmsBallisticDb.size(); ++ch)
    {
        const float channelRms = static_cast<float>(std::sqrt(outputStats[ch].energy
                                                              / juce::jmax(1, samples)));
        const float targetDb = juce::Decibels::gainToDecibels(channelRms, -100.0f);
        updateMeterBallistic(targetDb, outputRmsBallisticDb[ch], meters.outputRmsDb[ch],
                             samples, getSampleRate(), 0.300f, 0.650f);
    }
    const float actualGrDb = dsp.getGrDb();
    meters.grDb.store(actualGrDb);
    for (size_t ch = 0; ch < meters.grVuDb.size(); ++ch)
        meters.grVuDb[ch].store(dsp.getGrDbForChannel(static_cast<int>(ch)),
                                std::memory_order_relaxed);
    const float effectiveThresholdDb = dsp.getEffectiveThresholdDb();
    meters.effectiveThresholdDb.store(effectiveThresholdDb);
    meters.graphGainDb.store(dsp.getGraphGainDb(), std::memory_order_relaxed);

    const float blockSeconds = static_cast<float>(samples / getSampleRate());
    const float averageAlpha = 1.0f - std::exp(-blockSeconds / 0.5f);
    auto& currentAverage = grMatchAverageDb[(size_t) requestedMode];

    if (grMatchEnabled && grMatchActive)
    {
        grMatchElapsedSeconds += blockSeconds;
        const float settleSeconds = requestedMode == QuanpressorDSP::OPTO ? 0.90f
                                  : requestedMode == QuanpressorDSP::VARIMU ? 0.65f : 0.30f;
        const float windowLength = requestedMode == QuanpressorDSP::OPTO ? 1.00f
                                 : requestedMode == QuanpressorDSP::VARIMU ? 0.80f : 0.50f;

        // Compare fixed time windows instead of chasing program material block by block.
        if (grMatchElapsedSeconds > settleSeconds)
        {
            grMatchWindowGrSeconds += static_cast<double>(actualGrDb) * blockSeconds;
            grMatchWindowSeconds += blockSeconds;

            if (grMatchWindowSeconds >= windowLength)
            {
                const float measuredDb = static_cast<float>(grMatchWindowGrSeconds / grMatchWindowSeconds);
                currentAverage = measuredDb;
                const float errorDb = grMatchTargetDb - measuredDb;
                const float thresholdStep = juce::jlimit(-2.0f, 2.0f, errorDb * 0.55f);
                grMatchThresholdDb = juce::jlimit(-48.0f, 0.0f,
                    grMatchThresholdDb + thresholdStep);

                if (std::abs(errorDb) < 0.30f)
                    ++grMatchStableWindows;
                else
                    grMatchStableWindows = 0;

                const bool atLimit = grMatchThresholdDb <= -47.999f || grMatchThresholdDb >= -0.001f;
                if (std::abs(grMatchThresholdDb - c.thresholdDb) >= 0.05f)
                {
                    pendingThresholdDb.store(grMatchThresholdDb, std::memory_order_relaxed);
                    thresholdUpdatePending.store(true, std::memory_order_release);
                    triggerAsyncUpdate();
                }

                grMatchWindowGrSeconds = 0.0;
                grMatchWindowSeconds = 0.0f;
                if (grMatchStableWindows >= 2 || grMatchElapsedSeconds >= 10.0f || atLimit)
                    grMatchActive = false;
            }
        }
    }
    else
    {
        currentAverage += averageAlpha * (actualGrDb - currentAverage);
    }
    meters.grMatching.store(grMatchEnabled && grMatchActive, std::memory_order_relaxed);

    previousGrMatchMode = requestedMode;

    // The graph spot and INPUT peak meter describe the same post-input-gain
    // signal. Sharing the ballistic value avoids architecture-dependent jumps.
    graphDetectorDb = juce::jmax(inputPeakBallisticDb[0], inputPeakBallisticDb[1]);
    meters.graphInputDb.store(graphDetectorDb);

    // Each OUTPUT VU measures its final host-output channel after every DSP,
    // MAKEUP, MIX and OUTPUT operation.
    for (size_t ch = 0; ch < outputVu.size(); ++ch)
    {
        const double meanSquare = outputStats[ch].energy
                                / static_cast<double>(juce::jmax(1, samples));
        meters.outputVuDb[ch].store(outputVu[ch].process(meanSquare, samples),
                                    std::memory_order_relaxed);
    }

    historyCounter += samples;
    if (historyCounter > static_cast<int>(getSampleRate() / 20.0))
    {
        meters.pushHistory(dsp.getGrDb());
        historyCounter = 0;
    }
}
void QuanpressorAudioProcessor::handleAsyncUpdate()
{
    const auto disableGrMatch = [this]
    {
        if (auto* grMatch = apvts.getParameter("grMatch");
            grMatch != nullptr && grMatch->getValue() > 0.5f)
            grMatch->setValueNotifyingHost(0.0f);
        meters.grMatching.store(false, std::memory_order_relaxed);
    };

    if (grMatchCancellationPending.exchange(false, std::memory_order_acq_rel))
    {
        thresholdUpdatePending.store(false, std::memory_order_release);
        disableGrMatch();
        return;
    }

    if (!thresholdUpdatePending.exchange(false, std::memory_order_acq_rel))
        return;

    if (auto* threshold = apvts.getParameter("threshold"))
    {
        const float plainValue = pendingThresholdDb.load(std::memory_order_relaxed);
        processorThresholdExpectedDb.store(plainValue, std::memory_order_relaxed);
        processorThresholdWriteActive.store(true, std::memory_order_release);
        threshold->setValueNotifyingHost(threshold->convertTo0to1(plainValue));
        processorThresholdWriteActive.store(false, std::memory_order_release);

        // If host automation raced the internal write, the listener captured its
        // value. Restore that external value and relinquish GR Match ownership.
        if (grMatchCancellationPending.exchange(false, std::memory_order_acq_rel))
        {
            thresholdUpdatePending.store(false, std::memory_order_release);
            disableGrMatch();
            const float externalValue = lastExternalThresholdDb.load(std::memory_order_relaxed);
            processorThresholdExpectedDb.store(externalValue, std::memory_order_relaxed);
            processorThresholdWriteActive.store(true, std::memory_order_release);
            threshold->setValueNotifyingHost(threshold->convertTo0to1(externalValue));
            processorThresholdWriteActive.store(false, std::memory_order_release);
        }
    }
}

void QuanpressorAudioProcessor::parameterChanged(const juce::String& parameterId, float newValue)
{
    if (parameterId != "threshold" || restoringState.load(std::memory_order_acquire))
        return;

    const bool expectedProcessorWrite = processorThresholdWriteActive.load(std::memory_order_acquire)
        && std::abs(newValue - processorThresholdExpectedDb.load(std::memory_order_relaxed)) < 0.051f;
    if (expectedProcessorWrite)
        return;

    if (apvts.getRawParameterValue("grMatch")->load(std::memory_order_relaxed) > 0.5f)
    {
        lastExternalThresholdDb.store(newValue, std::memory_order_relaxed);
        thresholdUpdatePending.store(false, std::memory_order_release);
        grMatchCancellationPending.store(true, std::memory_order_release);
        meters.grMatching.store(false, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}
void QuanpressorAudioProcessor::getStateInformation(juce::MemoryBlock& d){ auto x=apvts.copyState().createXml(); copyXmlToBinary(*x,d); }
void QuanpressorAudioProcessor::setStateInformation(const void* d,int n)
{
    if (auto x = getXmlFromBinary(d,n))
    {
        restoringState.store(true, std::memory_order_release);
        apvts.replaceState(juce::ValueTree::fromXml(*x));
        restoringState.store(false, std::memory_order_release);
    }
}
juce::AudioProcessorEditor* QuanpressorAudioProcessor::createEditor(){ return new QuanpressorAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new QuanpressorAudioProcessor(); }
