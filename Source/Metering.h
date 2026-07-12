#pragma once
#include <atomic>
#include <array>

struct QuanpressorMeters
{
    std::atomic<float> inputDb {-100.0f};
    std::atomic<float> inputRmsDb {-100.0f};
    std::atomic<float> outputDb {-100.0f};
    std::atomic<float> outputRmsDb {-100.0f};
    std::atomic<float> grDb {0.0f};
    std::atomic<float> vuDb {-100.0f};
    std::atomic<float> graphInputDb {-100.0f};
    std::atomic<float> graphGainDb {0.0f};
    std::atomic<float> effectiveThresholdDb {-18.0f};
    std::atomic<bool> grMatching {false};
    std::array<std::atomic<float>, 160> history {};
    std::atomic<int> writeIndex {0};

    QuanpressorMeters()
    {
        for (auto& v : history)
            v.store(0.0f);
    }

    void pushHistory(float gr)
    {
        const auto i = writeIndex.fetch_add(1, std::memory_order_relaxed)
                     % static_cast<int>(history.size());
        history[static_cast<size_t>(i)].store(gr, std::memory_order_relaxed);
    }
};
