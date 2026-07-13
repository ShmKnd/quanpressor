#pragma once
#include <atomic>
#include <array>

struct QuanpressorMeters
{
    std::array<std::atomic<float>, 2> inputDb;
    std::array<std::atomic<float>, 2> inputRmsDb;
    std::array<std::atomic<float>, 2> outputDb;
    std::array<std::atomic<float>, 2> outputRmsDb;
    std::array<std::atomic<float>, 2> inputVuDb;
    std::array<std::atomic<float>, 2> outputVuDb;
    std::array<std::atomic<float>, 2> grVuDb;
    std::atomic<float> grDb {0.0f};
    std::atomic<float> graphInputDb {-100.0f};
    std::atomic<float> graphGainDb {0.0f};
    std::atomic<float> effectiveThresholdDb {-18.0f};
    std::atomic<bool> grMatching {false};
    std::array<std::atomic<float>, 160> history {};
    std::atomic<int> writeIndex {0};

    QuanpressorMeters()
    {
        for (auto* pair : { &inputDb, &inputRmsDb, &outputDb, &outputRmsDb })
            for (auto& value : *pair)
                value.store(-100.0f, std::memory_order_relaxed);

        for (auto* pair : { &inputVuDb, &outputVuDb })
            for (auto& value : *pair)
                value.store(-60.0f, std::memory_order_relaxed);

        for (auto& value : grVuDb)
            value.store(0.0f, std::memory_order_relaxed);

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
