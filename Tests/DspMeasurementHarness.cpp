#include "../Source/QuanpressorDSP.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

struct Configuration
{
    double sampleRate = 48000.0;
    int blockSize = 64;
    int channels = 2;
    bool linked = true;
    bool oversampling = false;
    int oversamplingFactor = 2;
    QuanpressorDSP::Mode mode = QuanpressorDSP::FET;
};

struct SignalStats
{
    double energy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    bool denormalFree = true;

    bool valid() const noexcept
    {
        return finite && denormalFree && peak < 32.0f;
    }
};

const char* modeName(QuanpressorDSP::Mode mode)
{
    switch (mode)
    {
        case QuanpressorDSP::FET: return "FET";
        case QuanpressorDSP::OPTO: return "OPTO";
        case QuanpressorDSP::VCA: return "VCA";
        case QuanpressorDSP::VARIMU: return "VARIMU";
    }
    return "UNKNOWN";
}

const char* topologyName(const Configuration& config)
{
    if (config.channels == 1)
        return "mono";
    return config.linked ? "stereo_linked" : "stereo_unlinked";
}

const char* qualityName(const Configuration& config)
{
    if (!config.oversampling)
        return "native";
    return config.oversamplingFactor >= 4 ? "4x" : "2x";
}

QuanpressorDSP::Controls makeControls(const Configuration& config)
{
    QuanpressorDSP::Controls controls;
    controls.mode = config.mode;
    controls.thresholdDb = -24.0f;
    controls.ratio = 4.0f;
    controls.attackMs = 10.0f;
    controls.releaseMs = 150.0f;
    controls.kneeDb = 6.0f;
    controls.stereoChain = config.linked;
    controls.oversampling = config.oversampling;
    controls.oversamplingFactor = config.oversamplingFactor;
    return controls;
}

void writeResult(std::ofstream& output,
                 const std::string& label,
                 const std::string& commit,
                 const Configuration& config,
                 const char* measurement,
                 double x,
                 double value,
                 const char* unit,
                 bool valid)
{
    output << label << ',' << commit << ',' << modeName(config.mode) << ','
           << std::fixed << std::setprecision(1) << config.sampleRate << ','
           << config.blockSize << ',' << topologyName(config) << ','
           << qualityName(config) << ',' << measurement << ','
           << std::setprecision(9) << x << ',' << value << ',' << unit << ','
           << (valid ? 1 : 0) << '\n';
}

template <typename Generator, typename Observer>
SignalStats render(QuanpressorDSP& dsp,
                   juce::AudioBuffer<float>& block,
                   QuanpressorDSP::Controls& controls,
                   int totalSamples,
                   Generator&& generator,
                   Observer&& observer)
{
    SignalStats stats;
    int sampleOffset = 0;
    while (sampleOffset < totalSamples)
    {
        const int samples = juce::jmin(block.getNumSamples(), totalSamples - sampleOffset);
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(),
                                      block.getNumChannels(), 0, samples);
        for (int channel = 0; channel < view.getNumChannels(); ++channel)
        {
            auto* data = view.getWritePointer(channel);
            for (int i = 0; i < samples; ++i)
                data[i] = generator(channel, sampleOffset + i);
        }

        dsp.process(view, controls);
        for (int channel = 0; channel < view.getNumChannels(); ++channel)
        {
            const auto* data = view.getReadPointer(channel);
            for (int i = 0; i < samples; ++i)
            {
                const float sample = data[i];
                stats.finite = stats.finite && std::isfinite(sample);
                stats.denormalFree = stats.denormalFree
                    && std::fpclassify(sample) != FP_SUBNORMAL;
                stats.peak = juce::jmax(stats.peak, std::abs(sample));
                stats.energy += static_cast<double>(sample) * static_cast<double>(sample);
                observer(channel, sampleOffset + i, sample);
            }
        }
        sampleOffset += samples;
    }
    return stats;
}

double gainToDb(double gain)
{
    return 20.0 * std::log10(std::max(gain, 1.0e-12));
}

double toneProjection(const std::vector<float>& signal, double frequency, double sampleRate)
{
    double real = 0.0;
    double imag = 0.0;
    double windowSum = 0.0;
    for (size_t i = 0; i < signal.size(); ++i)
    {
        const double phase = 2.0 * pi * frequency * static_cast<double>(i) / sampleRate;
        const double window = signal.size() > 1
            ? 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i)
                                  / static_cast<double>(signal.size() - 1))
            : 1.0;
        real += static_cast<double>(signal[i]) * window * std::cos(phase);
        imag -= static_cast<double>(signal[i]) * window * std::sin(phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt(real * real + imag * imag)
         / std::max(windowSum, 1.0e-12);
}

void measureStaticCurve(std::ofstream& output,
                        const std::string& label,
                        const std::string& commit,
                        const Configuration& config)
{
    for (int inputDb = -60; inputDb <= 0; inputDb += 3)
    {
        QuanpressorDSP dsp;
        dsp.prepare(config.sampleRate, config.blockSize, config.channels);
        auto controls = makeControls(config);
        juce::AudioBuffer<float> block(config.channels, config.blockSize);
        const float amplitude = static_cast<float>(std::pow(10.0, inputDb / 20.0));
        const int totalSamples = static_cast<int>(config.sampleRate * 0.40);
        const int measureStart = static_cast<int>(config.sampleRate * 0.25);
        double measuredEnergy = 0.0;
        int measuredCount = 0;

        const auto stats = render(dsp, block, controls, totalSamples,
            [&](int channel, int sample)
            {
                const double channelScale = (!config.linked && channel == 1) ? 0.5011872336 : 1.0;
                return static_cast<float>(amplitude * channelScale
                    * std::sin(2.0 * pi * 1000.0 * sample / config.sampleRate));
            },
            [&](int channel, int sample, float value)
            {
                if (sample >= measureStart && channel == 0)
                {
                    measuredEnergy += static_cast<double>(value) * static_cast<double>(value);
                    ++measuredCount;
                }
            });

        const double outputRms = std::sqrt(measuredEnergy / std::max(1, measuredCount));
        const double outputDb = gainToDb(outputRms * std::sqrt(2.0));
        writeResult(output, label, commit, config, "static_output", inputDb,
                    outputDb, "dBFS_peak", stats.valid());
        writeResult(output, label, commit, config, "gain_reduction", inputDb,
                    outputDb - inputDb, "dB", stats.valid());
    }
}

void measureHarmonics(std::ofstream& output,
                      const std::string& label,
                      const std::string& commit,
                      const Configuration& config,
                      float driveDb)
{
    QuanpressorDSP dsp;
    dsp.prepare(config.sampleRate, config.blockSize, config.channels);
    auto controls = makeControls(config);
    controls.driveDb = driveDb;
    juce::AudioBuffer<float> block(config.channels, config.blockSize);
    const int settleSamples = static_cast<int>(config.sampleRate * 0.30);
    const int captureSamples = 8192;
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(captureSamples));

    const auto stats = render(dsp, block, controls, settleSamples + captureSamples,
        [&](int, int sample)
        {
            return static_cast<float>(0.25
                * std::sin(2.0 * pi * 1000.0 * sample / config.sampleRate));
        },
        [&](int channel, int sample, float value)
        {
            if (channel == 0 && sample >= settleSamples)
                captured.push_back(value);
        });

    const double fundamental = toneProjection(captured, 1000.0, config.sampleRate);
    double harmonicEnergy = 0.0;
    for (int harmonic = 2; harmonic <= 12; ++harmonic)
    {
        const double frequency = 1000.0 * harmonic;
        if (frequency >= config.sampleRate * 0.5)
            break;
        const double amplitude = toneProjection(captured, frequency, config.sampleRate);
        harmonicEnergy += amplitude * amplitude;
        writeResult(output, label, commit, config, "harmonic", harmonic,
                    gainToDb(amplitude / std::max(fundamental, 1.0e-12)),
                    "dBc", stats.valid());
    }
    const double thd = std::sqrt(harmonicEnergy) / std::max(fundamental, 1.0e-12);
    writeResult(output, label, commit, config, "thd", driveDb,
                100.0 * thd, "percent", stats.valid());
}

double foldedFrequency(double frequency, double sampleRate)
{
    frequency = std::fmod(std::abs(frequency), sampleRate);
    if (frequency > sampleRate * 0.5)
        frequency = sampleRate - frequency;
    return frequency;
}

void measureAliasing(std::ofstream& output,
                     const std::string& label,
                     const std::string& commit,
                     const Configuration& config)
{
    QuanpressorDSP dsp;
    dsp.prepare(config.sampleRate, config.blockSize, config.channels);
    auto controls = makeControls(config);
    controls.driveDb = 18.0f;
    juce::AudioBuffer<float> block(config.channels, config.blockSize);
    const int settleSamples = static_cast<int>(config.sampleRate * 0.25);
    const int captureSamples = 8192;
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(captureSamples));
    const auto stats = render(dsp, block, controls, settleSamples + captureSamples,
        [&](int, int sample)
        {
            return static_cast<float>(0.35
                * std::sin(2.0 * pi * 10000.0 * sample / config.sampleRate));
        },
        [&](int channel, int sample, float value)
        {
            if (channel == 0 && sample >= settleSamples)
                captured.push_back(value);
        });

    const double fundamental = toneProjection(captured, 10000.0, config.sampleRate);
    double aliasEnergy = 0.0;
    for (int harmonic = 2; harmonic <= 12; ++harmonic)
    {
        const double aliasFrequency = foldedFrequency(10000.0 * harmonic, config.sampleRate);
        if (aliasFrequency < 20.0 || std::abs(aliasFrequency - 10000.0) < 1.0)
            continue;
        const double amplitude = toneProjection(captured, aliasFrequency, config.sampleRate);
        aliasEnergy += amplitude * amplitude;
    }
    writeResult(output, label, commit, config, "alias_energy", 10000.0,
                gainToDb(std::sqrt(aliasEnergy) / std::max(fundamental, 1.0e-12)),
                "dBc", stats.valid());
}

void measureIntermodulation(std::ofstream& output,
                            const std::string& label,
                            const std::string& commit,
                            const Configuration& config)
{
    QuanpressorDSP dsp;
    dsp.prepare(config.sampleRate, config.blockSize, config.channels);
    auto controls = makeControls(config);
    controls.driveDb = 12.0f;
    juce::AudioBuffer<float> block(config.channels, config.blockSize);
    const int settleSamples = static_cast<int>(config.sampleRate * 0.25);
    const int captureSamples = 8192;
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(captureSamples));
    const auto stats = render(dsp, block, controls, settleSamples + captureSamples,
        [&](int, int sample)
        {
            const double time = sample / config.sampleRate;
            return static_cast<float>(0.14 * std::sin(2.0 * pi * 19000.0 * time)
                                    + 0.14 * std::sin(2.0 * pi * 20000.0 * time));
        },
        [&](int channel, int sample, float value)
        {
            if (channel == 0 && sample >= settleSamples)
                captured.push_back(value);
        });

    const double carrier = std::max(toneProjection(captured, 19000.0, config.sampleRate),
                                    toneProjection(captured, 20000.0, config.sampleRate));
    const double difference = toneProjection(captured, 1000.0, config.sampleRate);
    writeResult(output, label, commit, config, "two_tone_imd", 1000.0,
                gainToDb(difference / std::max(carrier, 1.0e-12)),
                "dBc", stats.valid());

    QuanpressorDSP multitoneDsp;
    multitoneDsp.prepare(config.sampleRate, config.blockSize, config.channels);
    captured.clear();
    const std::array<double, 5> frequencies { 250.0, 500.0, 1000.0, 2000.0, 4000.0 };
    const auto multiStats = render(multitoneDsp, block, controls, settleSamples + captureSamples,
        [&](int, int sample)
        {
            double value = 0.0;
            for (const double frequency : frequencies)
                value += 0.055 * std::sin(2.0 * pi * frequency * sample / config.sampleRate);
            return static_cast<float>(value);
        },
        [&](int channel, int sample, float value)
        {
            if (channel == 0 && sample >= settleSamples)
                captured.push_back(value);
        });

    const double reference = toneProjection(captured, 1000.0, config.sampleRate);
    const std::array<double, 4> probeFrequencies { 750.0, 1250.0, 1500.0, 3000.0 };
    double probeEnergy = 0.0;
    for (const double frequency : probeFrequencies)
    {
        const double amplitude = toneProjection(captured, frequency, config.sampleRate);
        probeEnergy += amplitude * amplitude;
    }
    writeResult(output, label, commit, config, "multitone_imd", 0.0,
                gainToDb(std::sqrt(probeEnergy) / std::max(reference, 1.0e-12)),
                "dBc", multiStats.valid());
}

void measureTiming(std::ofstream& output,
                   const std::string& label,
                   const std::string& commit,
                   const Configuration& config)
{
    Configuration timingConfig = config;
    timingConfig.blockSize = config.mode == QuanpressorDSP::FET ? 1 : 16;
    QuanpressorDSP dsp;
    dsp.prepare(timingConfig.sampleRate, timingConfig.blockSize, timingConfig.channels);
    auto controls = makeControls(timingConfig);
    controls.attackMs = config.mode == QuanpressorDSP::FET ? 0.02f : 10.0f;
    controls.releaseMs = 300.0f;
    juce::AudioBuffer<float> block(timingConfig.channels, timingConfig.blockSize);
    const int quietSamples = static_cast<int>(config.sampleRate * 0.20);
    const int loudSamples = static_cast<int>(config.sampleRate * 0.60);
    const int releaseSamples = static_cast<int>(config.sampleRate * 2.50);
    const int totalSamples = quietSamples + loudSamples + releaseSamples;
    std::vector<float> grTrace;
    grTrace.reserve(static_cast<size_t>(
        (totalSamples + timingConfig.blockSize - 1) / timingConfig.blockSize));

    int sampleOffset = 0;
    while (sampleOffset < totalSamples)
    {
        const int samples = juce::jmin(
            timingConfig.blockSize, totalSamples - sampleOffset);
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(),
                                      block.getNumChannels(), 0, samples);
        for (int channel = 0; channel < view.getNumChannels(); ++channel)
        {
            auto* data = view.getWritePointer(channel);
            for (int i = 0; i < samples; ++i)
            {
                const int position = sampleOffset + i;
                const float amplitude = position >= quietSamples
                                     && position < quietSamples + loudSamples ? 0.5f : 0.01f;
                data[i] = amplitude;
            }
        }
        dsp.process(view, controls);
        grTrace.push_back(dsp.getInstantaneousGrDb());
        sampleOffset += samples;
    }

    const int attackBlock = quietSamples / timingConfig.blockSize;
    const int releaseBlock = (quietSamples + loudSamples) / timingConfig.blockSize;
    float steadyGr = 0.0f;
    for (int i = juce::jmax(attackBlock, releaseBlock - 4);
         i < juce::jmin(static_cast<int>(grTrace.size()), releaseBlock); ++i)
        steadyGr += grTrace[static_cast<size_t>(i)];
    steadyGr *= 0.25f;
    const float attackTarget = steadyGr * 0.6321205588f;
    const float releaseTarget = steadyGr * 0.3678794412f;
    double attackMs = std::numeric_limits<double>::quiet_NaN();
    double releaseMs = std::numeric_limits<double>::quiet_NaN();
    for (int i = attackBlock; i < releaseBlock && !std::isfinite(attackMs); ++i)
        if (grTrace[static_cast<size_t>(i)] <= attackTarget)
            attackMs = 1000.0 * (i - attackBlock + 1)
                     * timingConfig.blockSize / timingConfig.sampleRate;
    for (int i = releaseBlock; i < static_cast<int>(grTrace.size())
                               && !std::isfinite(releaseMs); ++i)
        if (grTrace[static_cast<size_t>(i)] >= releaseTarget)
            releaseMs = 1000.0 * (i - releaseBlock + 1)
                      * timingConfig.blockSize / timingConfig.sampleRate;

    writeResult(output, label, commit, timingConfig, "attack_t63", controls.attackMs,
                attackMs, "ms", std::isfinite(attackMs));
    writeResult(output, label, commit, timingConfig, "release_t63", controls.releaseMs,
                releaseMs, "ms", std::isfinite(releaseMs));
}

void measureGrDepthSweep(std::ofstream& output,
                         const std::string& label,
                         const std::string& commit,
                         const Configuration& config)
{
    for (int inputDb = -42; inputDb <= 0; inputDb += 3)
    {
        QuanpressorDSP dsp;
        dsp.prepare(config.sampleRate, config.blockSize, config.channels);
        auto controls = makeControls(config);
        controls.thresholdDb = -36.0f;
        controls.ratio = 20.0f;
        juce::AudioBuffer<float> block(config.channels, config.blockSize);
        const float amplitude = static_cast<float>(std::pow(10.0, inputDb / 20.0));
        const int totalSamples = static_cast<int>(config.sampleRate * 0.50);
        const int measureStart = static_cast<int>(config.sampleRate * 0.35);
        double measuredEnergy = 0.0;
        int measuredCount = 0;
        const auto stats = render(dsp, block, controls, totalSamples,
            [&](int, int sample)
            {
                return static_cast<float>(amplitude
                    * std::sin(2.0 * pi * 1000.0 * sample / config.sampleRate));
            },
            [&](int channel, int sample, float value)
            {
                if (channel == 0 && sample >= measureStart)
                {
                    measuredEnergy += static_cast<double>(value)
                                    * static_cast<double>(value);
                    ++measuredCount;
                }
            });
        const double outputPeakDb = gainToDb(
            std::sqrt(measuredEnergy / std::max(1, measuredCount))
            * std::sqrt(2.0));
        writeResult(output, label, commit, config, "gr_depth_sweep", inputDb,
                    outputPeakDb - inputDb, "dB",
                    stats.valid());
    }
}

void measureLatencyAlignment(std::ofstream& output,
                             const std::string& label,
                             const std::string& commit,
                             const Configuration& config)
{
    Configuration latencyConfig = config;
    latencyConfig.blockSize = 256;
    QuanpressorDSP dsp;
    dsp.prepare(
        latencyConfig.sampleRate,
        latencyConfig.blockSize,
        latencyConfig.channels);
    auto controls = makeControls(latencyConfig);
    controls.mix = 0.0f;
    juce::AudioBuffer<float> block(
        latencyConfig.channels,
        latencyConfig.blockSize);
    block.clear();
    block.setSample(0, 0, 1.0f);
    if (latencyConfig.channels > 1)
        block.setSample(1, 0, -0.5f);
    dsp.process(block, controls);

    const int latency = dsp.getLatencySamples();
    double maximumError = 0.0;
    bool finite = latency >= 0 && latency < block.getNumSamples();
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const float expectedImpulse = channel == 0 ? 1.0f : -0.5f;
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const float expected = sample == latency
                ? expectedImpulse : 0.0f;
            const float actual = block.getSample(channel, sample);
            finite = finite && std::isfinite(actual)
                && std::fpclassify(actual) != FP_SUBNORMAL;
            maximumError = std::max(
                maximumError,
                static_cast<double>(std::abs(actual - expected)));
        }
    }
    const double qualityFactor = !config.oversampling ? 1.0
        : config.oversamplingFactor >= 4 ? 4.0 : 2.0;
    writeResult(output, label, commit, latencyConfig, "latency",
                qualityFactor, latency, "samples", finite);
    writeResult(output, label, commit, latencyConfig,
                "latency_alignment_error", latency,
                maximumError, "linear",
                finite && maximumError <= 1.0e-6);
}

void measureAutomation(std::ofstream& output,
                       const std::string& label,
                       const std::string& commit,
                       const Configuration& baseConfig)
{
    constexpr std::array<int, 5> blockSizes { 1, 16, 64, 512, 8192 };
    for (const int blockSize : blockSizes)
    {
        Configuration config = baseConfig;
        config.blockSize = blockSize;
        QuanpressorDSP dsp;
        dsp.prepare(config.sampleRate, blockSize, config.channels);
        auto controls = makeControls(config);
        juce::AudioBuffer<float> block(config.channels, blockSize);
        float previousSample = 0.0f;
        float maximumJump = 0.0f;
        bool finite = true;
        int sampleOffset = 0;
        const int totalSamples = static_cast<int>(config.sampleRate * 1.0);
        while (sampleOffset < totalSamples)
        {
            const int samples = juce::jmin(blockSize, totalSamples - sampleOffset);
            const float position = static_cast<float>(sampleOffset)
                                 / static_cast<float>(juce::jmax(1, totalSamples - 1));
            controls.inputDb = juce::jmap(position, -12.0f, 12.0f);
            controls.outputDb = juce::jmap(position, 12.0f, -12.0f);
            controls.makeupDb = juce::jmap(position, -6.0f, 12.0f);
            controls.mix = position;
            controls.driveDb = juce::jmap(position, 0.0f, 18.0f);
            controls.thresholdDb = juce::jmap(position, -42.0f, -6.0f);
            controls.ratio = juce::jmap(position, 1.5f, 20.0f);
            controls.kneeDb = juce::jmap(position, 0.0f, 18.0f);
            juce::AudioBuffer<float> view(block.getArrayOfWritePointers(),
                                          block.getNumChannels(), 0, samples);
            for (int channel = 0; channel < view.getNumChannels(); ++channel)
                for (int i = 0; i < samples; ++i)
                    view.setSample(channel, i, static_cast<float>(0.1
                        * std::sin(2.0 * pi * 997.0 * (sampleOffset + i)
                                   / config.sampleRate)));
            dsp.process(view, controls);
            for (int i = 0; i < samples; ++i)
            {
                const float sample = view.getSample(0, i);
                finite = finite && std::isfinite(sample);
                maximumJump = juce::jmax(maximumJump, std::abs(sample - previousSample));
                previousSample = sample;
            }
            sampleOffset += samples;
        }
        writeResult(output, label, commit, config, "automation_max_jump",
                    blockSize, maximumJump, "linear", finite && maximumJump < 8.0f);
        writeResult(output, label, commit, config, "latency",
                    blockSize, dsp.getLatencySamples(), "samples", finite);
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "Usage: DspMeasurementHarness <label> <commit> <output.csv>\n";
        return 2;
    }

    const std::string label = argv[1];
    const std::string commit = argv[2];
    std::ofstream output(argv[3]);
    if (!output)
    {
        std::cerr << "Could not open output file\n";
        return 2;
    }
    output << "label,commit,mode,sample_rate,block_size,topology,quality,"
              "measurement,x,value,unit,valid\n";

    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<QuanpressorDSP::Mode, 4> modes {
        QuanpressorDSP::FET, QuanpressorDSP::OPTO,
        QuanpressorDSP::VCA, QuanpressorDSP::VARIMU
    };

    for (const double sampleRate : sampleRates)
    {
        for (const auto mode : modes)
        {
            for (int quality = 0; quality < 3; ++quality)
            {
                for (int topology = 0; topology < 3; ++topology)
                {
                    Configuration config;
                    config.sampleRate = sampleRate;
                    config.mode = mode;
                    config.channels = topology == 0 ? 1 : 2;
                    config.linked = topology != 2;
                    config.oversampling = quality != 0;
                    config.oversamplingFactor = quality == 2 ? 4 : 2;
                    measureStaticCurve(output, label, commit, config);
                }
            }

            Configuration reference;
            reference.sampleRate = sampleRate;
            reference.mode = mode;
            reference.channels = 2;
            reference.linked = true;
            for (int quality = 0; quality < 3; ++quality)
            {
                reference.oversampling = quality != 0;
                reference.oversamplingFactor = quality == 2 ? 4 : 2;
                for (int drive = 0; drive <= 18; drive += 3)
                    measureHarmonics(
                        output, label, commit, reference,
                        static_cast<float>(drive));
                measureAliasing(output, label, commit, reference);
                measureIntermodulation(output, label, commit, reference);
                measureGrDepthSweep(output, label, commit, reference);
                measureLatencyAlignment(
                    output, label, commit, reference);
            }
            reference.oversampling = false;
            measureTiming(output, label, commit, reference);
            measureAutomation(output, label, commit, reference);
        }
    }

    if (!output)
    {
        std::cerr << "Failed while writing measurement output\n";
        return 1;
    }
    return 0;
}
