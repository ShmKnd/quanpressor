#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 16;

void processConstant(QuanpressorDSP& dsp,
                     QuanpressorDSP::Controls& controls,
                     float amplitude,
                     double seconds,
                     int channels = 2)
{
    juce::AudioBuffer<float> block(channels, blockSize);
    int remaining = static_cast<int>(seconds * sampleRate);
    while (remaining > 0)
    {
        const int samples = juce::jmin(blockSize, remaining);
        juce::AudioBuffer<float> view(
            block.getArrayOfWritePointers(), channels, 0, samples);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                view.setSample(channel, sample, amplitude);
        dsp.process(view, controls);
        remaining -= samples;
    }
}

float steadyGainReduction(QuanpressorDSP::Mode mode,
                          float ratio,
                          float amplitude,
                          bool autoRelease = false)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.thresholdDb = -24.0f;
    controls.ratio = ratio;
    controls.kneeDb = mode == QuanpressorDSP::VCA ? 0.0f : 6.0f;
    controls.attackMs = 0.02f;
    controls.releaseMs = 300.0f;
    controls.autoRelease = autoRelease;
    controls.oversampling = false;
    controls.stereoChain = true;
    processConstant(dsp, controls, amplitude, 1.5);
    return dsp.getInstantaneousGrDb();
}

float optoRecoveryAfter(double loudSeconds)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::OPTO;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.attackMs = 10.0f;
    controls.releaseMs = 300.0f;
    controls.oversampling = false;
    processConstant(dsp, controls, 0.5f, loudSeconds);
    processConstant(dsp, controls, 0.001f, 0.35);
    return dsp.getInstantaneousGrDb();
}

float vcaAutoRecoveryAfter(double loudSeconds)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::VCA;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.kneeDb = 6.0f;
    controls.attackMs = 3.0f;
    controls.releaseMs = 300.0f;
    controls.autoRelease = true;
    controls.oversampling = false;
    processConstant(dsp, controls, 0.5f, loudSeconds);
    processConstant(dsp, controls, 0.001f, 0.30);
    return dsp.getInstantaneousGrDb();
}

float variableMuRecoveryAfter(double loudSeconds,
                              int timeConstant,
                              double recoverySeconds = 0.40)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = QuanpressorDSP::VARIMU;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.kneeDb = 6.0f;
    controls.varMuSpeed = timeConstant;
    controls.oversampling = false;
    processConstant(dsp, controls, 0.5f, loudSeconds);
    processConstant(dsp, controls, 0.001f, recoverySeconds);
    return dsp.getInstantaneousGrDb();
}

float measureOutputPeak(QuanpressorDSP::Mode mode, float amplitude)
{
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, 64, 2);
    QuanpressorDSP::Controls controls;
    controls.mode = mode;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.kneeDb = 6.0f;
    controls.attackMs = 0.02f;
    controls.releaseMs = 150.0f;
    controls.oversampling = false;

    juce::AudioBuffer<float> block(2, 64);
    float peak = 0.0f;
    int offset = 0;
    for (int blockIndex = 0; blockIndex < 1200; ++blockIndex)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = block.getWritePointer(channel);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                data[sample] = static_cast<float>(
                    amplitude * std::sin(
                        juce::MathConstants<double>::twoPi * 1000.0
                        * (offset + sample) / sampleRate));
            }
        }
        dsp.process(block, controls);
        if (blockIndex > 1000)
        {
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                peak = juce::jmax(peak, std::abs(block.getSample(0, sample)));
        }
        offset += block.getNumSamples();
    }
    return peak;
}
}

int main()
{
    const float vcaGr = steadyGainReduction(
        QuanpressorDSP::VCA, 4.0f,
        juce::Decibels::decibelsToGain(-12.0f));
    if (std::abs(vcaGr + 9.0f) > 0.35f)
    {
        std::cerr << "VCA dB-domain gain computer missed the expected 4:1 curve: "
                  << vcaGr << " dB\n";
        return 1;
    }

    constexpr std::array<float, 4> fetRatios { 4.0f, 8.0f, 12.0f, 20.0f };
    std::array<float, fetRatios.size()> fetReductions {};
    for (size_t i = 0; i < fetRatios.size(); ++i)
    {
        fetReductions[i] = steadyGainReduction(
            QuanpressorDSP::FET, fetRatios[i], 0.5f);
        if (!std::isfinite(fetReductions[i]) || fetReductions[i] > 0.0f)
        {
            std::cerr << "FET feedback loop produced an invalid gain state\n";
            return 1;
        }
    }
    for (size_t i = 1; i < fetReductions.size(); ++i)
    {
        if (fetReductions[i] >= fetReductions[i - 1] - 0.25f)
        {
            std::cerr << "FET ratio modes are not dynamically distinct\n";
            return 1;
        }
    }

    QuanpressorDSP fetAttackDsp;
    fetAttackDsp.prepare(sampleRate, 1, 1);

    const float fastestFetAttack = QuanpressorDSP::fetAttackMilliseconds(0.02f);
    const float slowestFetAttack = QuanpressorDSP::fetAttackMilliseconds(100.0f);
    if (std::abs(fastestFetAttack - 0.02f) > 1.0e-6f
        || std::abs(slowestFetAttack - 0.8f) > 1.0e-6f)
    {
        std::cerr << "FET Attack display mapping endpoints are incorrect: "
                  << fastestFetAttack << " to " << slowestFetAttack << " ms\n";
        return 1;
    }

    constexpr std::array<float, 5> fetAttackScaleParameters {
        0.02f, 10.0f, 30.0f, 60.0f, 100.0f
    };
    constexpr std::array<float, 5> fetAttackScaleMilliseconds {
        0.02f, 0.27f, 0.45f, 0.62f, 0.80f
    };
    for (size_t i = 0; i < fetAttackScaleParameters.size(); ++i)
    {
        const float mapped = QuanpressorDSP::fetAttackMilliseconds(
            fetAttackScaleParameters[i]);
        if (std::abs(mapped - fetAttackScaleMilliseconds[i]) > 0.005f)
        {
            std::cerr << "FET Attack scale mapping is incorrect at "
                      << fetAttackScaleParameters[i] << ": "
                      << mapped << " ms\n";
            return 1;
        }
    }

    QuanpressorDSP::Controls fetControls;
    fetControls.mode = QuanpressorDSP::FET;
    fetControls.thresholdDb = -24.0f;
    fetControls.ratio = 4.0f;
    fetControls.attackMs = 0.02f;
    fetControls.releaseMs = 300.0f;
    fetControls.oversampling = false;
    processConstant(fetAttackDsp, fetControls, 0.001f, 0.10, 1);
    const float steadyFetGr = steadyGainReduction(
        QuanpressorDSP::FET, 4.0f, 0.5f);
    const float attackTarget = steadyFetGr * 0.6321205588f;
    juce::AudioBuffer<float> oneSample(1, 1);
    int attackSamples = 0;
    while (attackSamples < 240)
    {
        oneSample.setSample(0, 0, 0.5f);
        fetAttackDsp.process(oneSample, fetControls);
        ++attackSamples;
        if (fetAttackDsp.getInstantaneousGrDb() <= attackTarget)
            break;
    }
    if (attackSamples >= 96)
    {
        std::cerr << "FET fastest attack exceeded 2 ms: "
                  << 1000.0 * attackSamples / sampleRate << " ms\n";
        return 1;
    }

    const float shortOptoRecovery = optoRecoveryAfter(0.05);
    const float longOptoRecovery = optoRecoveryAfter(2.0);
    if (!(longOptoRecovery < shortOptoRecovery - 0.15f))
    {
        std::cerr << "OPTO slow memory did not retain more GR after sustained input: "
                  << shortOptoRecovery << " vs " << longOptoRecovery << " dB\n";
        return 1;
    }

    const float shortVcaRecovery = vcaAutoRecoveryAfter(0.05);
    const float longVcaRecovery = vcaAutoRecoveryAfter(2.0);
    if (!(longVcaRecovery < shortVcaRecovery - 0.10f))
    {
        std::cerr << "VCA dual auto-release states did not distinguish peak and sustained input: "
                  << shortVcaRecovery << " vs " << longVcaRecovery << " dB\n";
        return 1;
    }

    const float shortMuRecovery = variableMuRecoveryAfter(0.05, 1);
    const float longMuRecovery = variableMuRecoveryAfter(2.0, 1);
    if (!(longMuRecovery < shortMuRecovery - 0.10f))
    {
        std::cerr << "Vari-Mu slow release did not retain more GR after sustained input: "
                  << shortMuRecovery << " vs " << longMuRecovery << " dB\n";
        return 1;
    }
    const float fastMuRecovery = variableMuRecoveryAfter(1.5, 0);
    const float slowMuRecovery = variableMuRecoveryAfter(1.5, 5);
    if (!(slowMuRecovery < fastMuRecovery - 0.25f))
    {
        std::cerr << "Vari-Mu time constants did not produce distinct release behavior: "
                  << fastMuRecovery << " vs " << slowMuRecovery << " dB\n";
        return 1;
    }

    const float setting5SinglePeak = variableMuRecoveryAfter(0.05, 4, 2.0);
    const float setting5MultiplePeaks = variableMuRecoveryAfter(2.0, 4, 2.0);
    if (!(setting5MultiplePeaks < setting5SinglePeak - 1.0f))
    {
        std::cerr << "Vari-Mu setting 5 did not charge its 10-second memory: "
                  << setting5SinglePeak << " vs "
                  << setting5MultiplePeaks << " dB\n";
        return 1;
    }

    const float setting6SinglePeak = variableMuRecoveryAfter(0.05, 5, 0.40);
    const float setting6Sustained = variableMuRecoveryAfter(8.0, 5, 0.40);
    if (!(setting6Sustained < setting6SinglePeak - 2.0f))
    {
        std::cerr << "Vari-Mu setting 6 did not charge its sustained memory: "
                  << setting6SinglePeak << " vs "
                  << setting6Sustained << " dB\n";
        return 1;
    }

    const float setting5FiveSecondTail = variableMuRecoveryAfter(8.0, 4, 5.0);
    const float setting6FiveSecondTail = variableMuRecoveryAfter(8.0, 5, 5.0);
    if (!(setting6FiveSecondTail < setting5FiveSecondTail - 1.0f))
    {
        std::cerr << "Vari-Mu setting 6 did not retain its 25-second tail: "
                  << setting5FiveSecondTail << " vs "
                  << setting6FiveSecondTail << " dB\n";
        return 1;
    }

    float previousOutput = 0.0f;
    for (int inputDb = -48; inputDb <= 0; inputDb += 3)
    {
        const float amplitude = juce::Decibels::decibelsToGain(
            static_cast<float>(inputDb));
        const float output = measureOutputPeak(QuanpressorDSP::VARIMU, amplitude);
        if (!std::isfinite(output) || output + 1.0e-5f < previousOutput)
        {
            std::cerr << "Vari-Mu transfer is not monotonic at "
                      << inputDb << " dBFS: "
                      << previousOutput << " to " << output << '\n';
            return 1;
        }
        previousOutput = output;
    }

    std::cout << "PASS: architecture-specific detector, timing, memory, and feedback behavior\n";
    return 0;
}
