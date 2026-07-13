#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
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
        const bool limiting = ratio >= 8.0f;
        const float compressionFactor = limiting ? 20.0f : 5.0f;
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
  apvts(*this,nullptr,"PARAMS",makeLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout QuanpressorAudioProcessor::makeLayout()
{
    using namespace juce; std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back(std::make_unique<AudioParameterChoice>("mode","Mode",StringArray{"FET","OPTO","VCA","VARI-MU"},0));
    auto add=[&](const char* id,const char* name,float a,float b,float step,float def,const char* suffix=""){ p.push_back(std::make_unique<AudioParameterFloat>(id,name,NormalisableRange<float>(a,b,step),def,String(),AudioProcessorParameter::genericParameter,[suffix](float v,int){return String(v,1)+suffix;},nullptr));};
    add("input","Input",-24,24,0.1f,0," dB"); add("threshold","Threshold",-48,0,0.1f,-18," dB"); add("ratio","Ratio",1.5f,20.0f,0.1f,4,":1"); add("drive","Drive",0,18,0.1f,0," dB");
    add("attack","Attack",0.02f,100,0.01f,10," ms"); add("release","Release",20,1200,1,150," ms"); add("knee","Knee",0,18,0.1f,6," dB");
    add("makeup","Makeup",-12,24,0.1f,0," dB"); add("highFreq","High Bleed",1000,18000,1,8000," Hz"); add("highAmt","High Amount",0,1,0.001f,0);
    add("lowFreq","Low Bleed",30,600,1,120," Hz"); add("lowAmt","Low Amount",0,1,0.001f,0); add("output","Output",-24,24,0.1f,0," dB");
    add("mix","Mix",0,100,0.1f,100," %");
    p.push_back(std::make_unique<AudioParameterChoice>("muSpeed","Time Constant",StringArray{"1","2","3","4","5","6"},2));
    p.push_back(std::make_unique<AudioParameterChoice>("vuMode","VU Mode",StringArray{"OUTPUT","INPUT","GR"},0));
    p.push_back(std::make_unique<AudioParameterBool>("autoLevel","Auto Level Match",false));
    p.push_back(std::make_unique<AudioParameterBool>("autoRelease","Auto Release",false));
    p.push_back(std::make_unique<AudioParameterBool>("grMatch","GR Match",false));
    p.push_back(std::make_unique<AudioParameterBool>("noMeter","No Meter",false));
    p.push_back(std::make_unique<AudioParameterBool>("sidechainHpf","Sidechain HPF",false));
    p.push_back(std::make_unique<AudioParameterBool>("stereoChain","Stereo Chain",true));
    return {p.begin(),p.end()};
}
void QuanpressorAudioProcessor::prepareToPlay(double sr,int bs)
{
    dsp.prepare(sr,bs,getTotalNumOutputChannels());
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
    const int channels = b.getNumChannels();

    std::array<float, 2> rawInputPeaks {};
    std::array<double, 2> rawInputEnergies {};
    for (int ch = 0; ch < channels; ++ch)
    {
        const float channelPeak = b.getMagnitude(ch, 0, samples);
        if (ch < 2)
            rawInputPeaks[static_cast<size_t>(ch)] = channelPeak;

        const auto* inputData = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
        {
            const double square = static_cast<double>(inputData[i]) * static_cast<double>(inputData[i]);
            if (ch < 2)
                rawInputEnergies[static_cast<size_t>(ch)] += square;
        }
    }

    if (channels == 1)
    {
        rawInputPeaks[1] = rawInputPeaks[0];
        rawInputEnergies[1] = rawInputEnergies[0];
    }

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

    const float inputGainLinear = juce::Decibels::decibelsToGain(c.inputDb);
    // Each INPUT VU follows its channel after INPUT GAIN. Mono buses duplicate
    // channel zero above so the two needles behave as a dual-mono meter pair.
    const double inputGainPower = static_cast<double>(inputGainLinear)
                                * static_cast<double>(inputGainLinear);
    for (size_t ch = 0; ch < inputVu.size(); ++ch)
    {
        const double meanSquare = rawInputEnergies[ch] * inputGainPower
                                / static_cast<double>(juce::jmax(1, samples));
        meters.inputVuDb[ch].store(inputVu[ch].process(meanSquare, samples),
                                   std::memory_order_relaxed);
    }
    std::array<float, 2> inputPeakTargetDb {};
    for (size_t ch = 0; ch < inputPeakTargetDb.size(); ++ch)
    {
        inputPeakTargetDb[ch] = juce::Decibels::gainToDecibels(rawInputPeaks[ch] * inputGainLinear, -100.0f);
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
        const float channelRms = static_cast<float>(std::sqrt(rawInputEnergies[ch]
                                                              / juce::jmax(1, samples)));
        const float targetDb = juce::Decibels::gainToDecibels(channelRms * inputGainLinear, -100.0f);
        updateMeterBallistic(targetDb, inputRmsBallisticDb[ch], meters.inputRmsDb[ch],
                             samples, getSampleRate(), 0.300f, 0.650f);
    }

    const bool grMatchEnabled = c.grMatch;
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

    std::array<float, 2> outputPeaks {};
    std::array<double, 2> outputEnergies {};
    for (int ch = 0; ch < channels; ++ch)
    {
        const float channelPeak = b.getMagnitude(ch, 0, samples);
        if (ch < 2)
            outputPeaks[static_cast<size_t>(ch)] = channelPeak;

        const auto* data = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
        {
            const double square = static_cast<double>(data[i]) * static_cast<double>(data[i]);
            if (ch < 2)
                outputEnergies[static_cast<size_t>(ch)] += square;
        }
    }

    if (channels == 1)
    {
        outputPeaks[1] = outputPeaks[0];
        outputEnergies[1] = outputEnergies[0];
    }

    for (size_t ch = 0; ch < outputPeakBallisticDb.size(); ++ch)
    {
        const float targetDb = juce::Decibels::gainToDecibels(outputPeaks[ch], -100.0f);
        updateMeterBallistic(targetDb, outputPeakBallisticDb[ch], meters.outputDb[ch],
                             samples, getSampleRate(), 0.008f, 0.140f);
    }
    for (size_t ch = 0; ch < outputRmsBallisticDb.size(); ++ch)
    {
        const float channelRms = static_cast<float>(std::sqrt(outputEnergies[ch]
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
        const double meanSquare = outputEnergies[ch]
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
    if (auto* threshold = apvts.getParameter("threshold"))
    {
        const float plainValue = pendingThresholdDb.load(std::memory_order_relaxed);
        const float normalized = threshold->convertTo0to1(plainValue);
        threshold->setValueNotifyingHost(normalized);
    }
}
void QuanpressorAudioProcessor::getStateInformation(juce::MemoryBlock& d){ auto x=apvts.copyState().createXml(); copyXmlToBinary(*x,d); }
void QuanpressorAudioProcessor::setStateInformation(const void* d,int n){ if(auto x=getXmlFromBinary(d,n)) apvts.replaceState(juce::ValueTree::fromXml(*x)); }
juce::AudioProcessorEditor* QuanpressorAudioProcessor::createEditor(){ return new QuanpressorAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new QuanpressorAudioProcessor(); }
