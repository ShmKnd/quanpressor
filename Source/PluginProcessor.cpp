#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
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
    outputVu.prepare(sr);
    graphDetectorDb = -100.0f;
    inputPeakBallisticDb = -100.0f;
    outputPeakBallisticDb = -100.0f;
    inputRmsBallisticDb = -100.0f;
    outputRmsBallisticDb = -100.0f;
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

    float rawInputPeak = 0.0f;
    double rawInputEnergy = 0.0;
    for (int ch = 0; ch < channels; ++ch)
    {
        rawInputPeak = juce::jmax(rawInputPeak, b.getMagnitude(ch, 0, samples));
        const auto* inputData = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            rawInputEnergy += static_cast<double>(inputData[i]) * static_cast<double>(inputData[i]);
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
    const float inputPeakTargetDb = juce::Decibels::gainToDecibels(rawInputPeak * inputGainLinear, -100.0f);
    {
        const float timeSeconds = inputPeakTargetDb > inputPeakBallisticDb ? 0.008f : 0.140f;
        const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                          / static_cast<float>(getSampleRate() * timeSeconds));
        inputPeakBallisticDb += alpha * (inputPeakTargetDb - inputPeakBallisticDb);
        meters.inputDb.store(inputPeakBallisticDb);
    }
    {
        if (grMatchInputLevelDb <= -99.0f && inputPeakTargetDb > -99.0f)
            grMatchInputLevelDb = inputPeakTargetDb;
        else
        {
            const float timeSeconds = inputPeakTargetDb > grMatchInputLevelDb ? 0.20f : 1.00f;
            const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                              / static_cast<float>(getSampleRate() * timeSeconds));
            grMatchInputLevelDb += alpha * (inputPeakTargetDb - grMatchInputLevelDb);
        }
    }
    const float inputRmsPre = static_cast<float>(std::sqrt(rawInputEnergy / juce::jmax(1, samples * channels)));
    const float inputRmsTargetDb = juce::Decibels::gainToDecibels(inputRmsPre * inputGainLinear, -100.0f);
    {
        const float timeSeconds = inputRmsTargetDb > inputRmsBallisticDb ? 0.300f : 0.650f;
        const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                          / static_cast<float>(getSampleRate() * timeSeconds));
        inputRmsBallisticDb += alpha * (inputRmsTargetDb - inputRmsBallisticDb);
        meters.inputRmsDb.store(inputRmsBallisticDb);
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

    float outputPeak = 0.0f;
    double outputEnergy = 0.0;
    for (int ch = 0; ch < channels; ++ch)
    {
        outputPeak = juce::jmax(outputPeak, b.getMagnitude(ch, 0, samples));
        const auto* data = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            outputEnergy += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }

    const float outputPeakTargetDb = juce::Decibels::gainToDecibels(outputPeak, -100.0f);
    {
        const float timeSeconds = outputPeakTargetDb > outputPeakBallisticDb ? 0.008f : 0.140f;
        const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                          / static_cast<float>(getSampleRate() * timeSeconds));
        outputPeakBallisticDb += alpha * (outputPeakTargetDb - outputPeakBallisticDb);
        meters.outputDb.store(outputPeakBallisticDb);
    }
    const float outputRms = static_cast<float>(std::sqrt(outputEnergy / juce::jmax(1, samples * channels)));
    const float outputRmsDb = juce::Decibels::gainToDecibels(outputRms, -100.0f);
    {
        const float timeSeconds = outputRmsDb > outputRmsBallisticDb ? 0.300f : 0.650f;
        const float alpha = 1.0f - std::exp(-static_cast<float>(samples)
                                          / static_cast<float>(getSampleRate() * timeSeconds));
        outputRmsBallisticDb += alpha * (outputRmsDb - outputRmsBallisticDb);
        meters.outputRmsDb.store(outputRmsBallisticDb);
    }
    const float actualGrDb = dsp.getGrDb();
    meters.grDb.store(actualGrDb);
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
    graphDetectorDb = inputPeakBallisticDb;
    meters.graphInputDb.store(graphDetectorDb);

    // This energy is measured from the host output buffer after every DSP,
    // MAKEUP, MIX and OUTPUT operation. VU has no parameter-side state.
    const double blockMeanSquare = outputEnergy / static_cast<double>(juce::jmax(1, samples * channels));
    meters.vuDb.store(outputVu.process(blockMeanSquare, samples));

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
