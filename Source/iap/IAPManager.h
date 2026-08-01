#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace soundrave::iap
{

class IAPManager final : public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    enum class State
    {
        Unavailable,
        ExtensionLocked,
        Loading,
        Ready,
        Purchasing,
        Restoring,
        Pending,
        Unlocked,
        Failed
    };

    static IAPManager& getInstance();

    void initialise(const juce::String& productName);
    void purchase();
    void restore();
    void refreshEntitlement();

    bool isUnlocked() const noexcept { return unlocked.load(std::memory_order_acquire); }
    bool isAudioAllowed() const noexcept
    {
        return isUnlocked() || demoAccessAllowed.load(std::memory_order_acquire);
    }
    bool canManagePurchasesHere() const noexcept;
    State getState() const noexcept { return static_cast<State>(state.load(std::memory_order_acquire)); }
    juce::String getStatusString() const;
    juce::String getPrice() const;
    juce::String getProductId() const;

    // Called only by the StoreKit 2 bridge callback.
    void handleBridgeEvent(std::int32_t bridgeState,
                           const char* message,
                           const char* displayPrice);

private:
    struct Configuration
    {
        juce::String productId;
        juce::String appGroupId;
        juce::String appBundleId;
        juce::String legacyPaidThroughVersion;
    };

    IAPManager() = default;
    ~IAPManager() override;

    static Configuration configurationForName(const juce::String& productName);
    void timerCallback() override;
    void updateDemoAccess();
    void syncSharedEntitlement();
    void setState(State newState, const juce::String& message = {});
    bool isBusy() const noexcept;

    mutable std::mutex mutex;
    juce::String configuredProductName;
    juce::String productId;
    juce::String appGroupId;
    juce::String appBundleId;
    juce::String legacyPaidThroughVersion;
    juce::String price;
    juce::String statusMessage;
    std::atomic<bool> unlocked { false };
    std::atomic<bool> demoAccessKnown { false };
    std::atomic<bool> demoAccessAllowed { false };
    std::atomic<std::int64_t> demoSecondsRemaining { 0 };
    std::atomic<bool> operationInFlight { false };
    std::atomic<int> state { static_cast<int>(State::Unavailable) };
    std::atomic<bool> configured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IAPManager)
};

} // namespace soundrave::iap
