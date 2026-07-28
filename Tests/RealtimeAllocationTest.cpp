#include "../Source/QuanpressorDSP.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
std::atomic<bool> trackAllocations { false };
std::atomic<unsigned long long> allocationCount { 0 };

void countAllocation() noexcept
{
    if (trackAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
}
}

void* operator new(std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    QuanpressorDSP dsp;
    dsp.prepare(sampleRate, blockSize, 2);
    QuanpressorDSP::Controls controls;
    controls.thresholdDb = -30.0f;
    controls.ratio = 8.0f;
    controls.driveDb = 6.0f;
    controls.oversampling = false;
    juce::AudioBuffer<float> block(2, blockSize);
    int sampleOffset = 0;

    const auto processBlock = [&]
    {
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* data = block.getWritePointer(channel);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                data[sample] = static_cast<float>(
                    0.25 * std::sin(
                        juce::MathConstants<double>::twoPi * 997.0
                        * (sampleOffset + sample) / sampleRate));
            }
        }
        dsp.process(block, controls);
        sampleOffset += blockSize;
    };

    for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
        processBlock();

    allocationCount.store(0, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
    {
        controls.inputDb = static_cast<float>((blockIndex % 25) - 12);
        controls.driveDb = static_cast<float>(blockIndex % 19);
        controls.thresholdDb = -42.0f + static_cast<float>(blockIndex % 30);
        controls.ratio = 1.5f + static_cast<float>(blockIndex % 18);
        if (blockIndex == 80)
            controls.mode = QuanpressorDSP::OPTO;
        if (blockIndex == 160)
            controls.mode = QuanpressorDSP::VCA;
        if (blockIndex == 240)
        {
            controls.oversampling = true;
            controls.oversamplingFactor = 2;
        }
        if (blockIndex == 320)
            controls.oversamplingFactor = 4;
        processBlock();
    }
    trackAllocations.store(false, std::memory_order_release);

    const auto allocations = allocationCount.load(std::memory_order_relaxed);
    if (allocations != 0)
    {
        std::cerr << "Audio processing allocated " << allocations
                  << " time(s) after prepare\n";
        return 1;
    }

    std::cout << "PASS: no heap allocation during continuous, mode, or quality processing\n";
    return 0;
}
