#include "IAPManager.h"
#include "IAPProductConfig.h"

#if JUCE_IOS
extern "C"
{
using SoundRaveStoreKitCallback = void (*)(std::int32_t, const char*, const char*);

void soundrave_storekit_configure(const char* productId,
                                  const char* appBundleId,
                                  const char* appGroupId,
                                  const char* legacyPaidThroughVersion,
                                  SoundRaveStoreKitCallback callback);
std::int32_t soundrave_storekit_is_standalone();
void soundrave_storekit_purchase();
void soundrave_storekit_restore();
void soundrave_storekit_refresh();
std::int32_t soundrave_storekit_read_shared_entitlement(const char* productId,
                                                        const char* appBundleId,
                                                        const char* appGroupId,
                                                        const char* legacyPaidThroughVersion);
std::int32_t soundrave_storekit_update_demo_access(const char* productId,
                                                   const char* appBundleId,
                                                   const char* appGroupId,
                                                   std::int64_t* secondsRemaining);
}
#endif

namespace soundrave::iap
{

namespace
{
#if JUCE_IOS
void storeKitCallback(std::int32_t state, const char* message, const char* displayPrice)
{
    IAPManager::getInstance().handleBridgeEvent(state, message, displayPrice);
}
#endif

juce::String twoDigits(std::int64_t value)
{
    return juce::String(value).paddedLeft('0', 2);
}

juce::String formatRemainingTime(std::int64_t seconds)
{
    seconds = juce::jmax<std::int64_t>(0, seconds);

    if (seconds >= 3600)
    {
        const auto hours = seconds / 3600;
        const auto minutes = (seconds % 3600) / 60;
        const auto remainingSeconds = seconds % 60;
        return twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(remainingSeconds);
    }

    return twoDigits(seconds / 60) + ":" + twoDigits(seconds % 60);
}
}

IAPManager& IAPManager::getInstance()
{
    static IAPManager instance;
    return instance;
}

IAPManager::~IAPManager()
{
    stopTimer();
}

void IAPManager::initialise(const juce::String& requestedProductName)
{
    const auto requestedConfiguration = configurationForName(requestedProductName);
    bool alreadyConfigured = false;

    {
        const std::lock_guard<std::mutex> lock(mutex);
        alreadyConfigured = configured.load(std::memory_order_acquire)
                         && configuredProductName == requestedProductName;

        if (!alreadyConfigured)
        {
            configuredProductName = requestedProductName;
            productId = requestedConfiguration.productId;
            appGroupId = requestedConfiguration.appGroupId;
            appBundleId = requestedConfiguration.appBundleId;
            legacyPaidThroughVersion = requestedConfiguration.legacyPaidThroughVersion;
            price.clear();
            statusMessage.clear();
            demoAccessKnown.store(false, std::memory_order_release);
            demoAccessAllowed.store(false, std::memory_order_release);
            demoSecondsRemaining.store(0, std::memory_order_release);
            configured.store(productId.isNotEmpty()
                          && appGroupId.isNotEmpty()
                          && appBundleId.isNotEmpty()
                          && legacyPaidThroughVersion.isNotEmpty(),
                             std::memory_order_release);
        }
    }

    if (alreadyConfigured)
    {
        refreshEntitlement();
        return;
    }

#if JUCE_IOS
    if (!configured.load(std::memory_order_acquire))
    {
        setState(State::Unavailable, "The in-app purchase configuration is invalid.");
        return;
    }

    juce::String configuredProductId;
    juce::String configuredBundleId;
    juce::String configuredGroupId;
    juce::String configuredLegacyVersion;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        configuredProductId = productId;
        configuredBundleId = appBundleId;
        configuredGroupId = appGroupId;
        configuredLegacyVersion = legacyPaidThroughVersion;
    }

    const auto sharedState = soundrave_storekit_read_shared_entitlement(
        configuredProductId.toRawUTF8(),
        configuredBundleId.toRawUTF8(),
        configuredGroupId.toRawUTF8(),
        configuredLegacyVersion.toRawUTF8());
    unlocked.store(sharedState == 1, std::memory_order_release);
    updateDemoAccess();

    if (!canManagePurchasesHere())
    {
        setState(sharedState == 1 ? State::Unlocked
                                  : sharedState == 0 ? State::ExtensionLocked
                                                     : State::Unavailable);
        startTimerHz(1);
        return;
    }

    juce::Desktop::getInstance().setOrientationsEnabled(
        juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);

    setState(sharedState == 1 ? State::Unlocked : State::Loading);
    startTimerHz(1);
    soundrave_storekit_configure(configuredProductId.toRawUTF8(),
                                 configuredBundleId.toRawUTF8(),
                                 configuredGroupId.toRawUTF8(),
                                 configuredLegacyVersion.toRawUTF8(),
                                 storeKitCallback);
#else
    juce::ignoreUnused(requestedConfiguration);
    setState(State::Unavailable);
#endif
}

void IAPManager::purchase()
{
#if JUCE_IOS
    const auto currentState = getState();
    const bool canRetry = currentState == State::Failed && getPrice().isNotEmpty();
    if (!canManagePurchasesHere() || (currentState != State::Ready && !canRetry))
        return;

    bool expected = false;
    if (!operationInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    setState(State::Purchasing, "Completing purchase...");
    soundrave_storekit_purchase();
#endif
}

void IAPManager::restore()
{
#if JUCE_IOS
    if (!canManagePurchasesHere() || isBusy() || getState() == State::Unavailable)
        return;

    bool expected = false;
    if (!operationInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    setState(State::Restoring, "Restoring purchases...");
    soundrave_storekit_restore();
#endif
}

void IAPManager::refreshEntitlement()
{
#if JUCE_IOS
    if (!configured.load(std::memory_order_acquire))
        return;

    if (canManagePurchasesHere())
    {
        if (!isBusy())
            soundrave_storekit_refresh();
        return;
    }

    syncSharedEntitlement();
#endif
}

bool IAPManager::canManagePurchasesHere() const noexcept
{
#if JUCE_IOS
    return !juce::SystemStats::isRunningInAppExtensionSandbox()
        && soundrave_storekit_is_standalone() == 1;
#else
    return false;
#endif
}

juce::String IAPManager::getStatusString() const
{
    if (isUnlocked())
        return "Full version unlocked";

    juce::String currentPrice;
    juce::String currentMessage;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        currentPrice = price;
        currentMessage = statusMessage;
    }

    const auto currentState = getState();
    if (currentMessage.isNotEmpty()
        && (currentState == State::Purchasing
            || currentState == State::Restoring
            || currentState == State::Pending
            || currentState == State::Failed))
        return currentMessage;

    if (demoAccessKnown.load(std::memory_order_acquire))
    {
        const auto remaining = demoSecondsRemaining.load(std::memory_order_acquire);
        if (demoAccessAllowed.load(std::memory_order_acquire))
            return "DEMO " + formatRemainingTime(remaining);

        return "LOCKED " + formatRemainingTime(remaining);
    }

    if (currentMessage.isNotEmpty())
        return currentMessage;

    switch (currentState)
    {
        case State::Unavailable:     return "AUDIO DISABLED - DEMO STATUS UNAVAILABLE";
        case State::ExtensionLocked: return "AUDIO DISABLED - DEMO STATUS UNAVAILABLE";
        case State::Loading:         return "Loading purchase information...";
        case State::Ready:           return currentPrice.isNotEmpty() ? "Demo - unlock for " + currentPrice
                                                                      : "Demo version";
        case State::Purchasing:      return "Completing purchase...";
        case State::Restoring:       return "Restoring purchases...";
        case State::Pending:         return "Purchase pending approval.";
        case State::Unlocked:        return "Full version unlocked";
        case State::Failed:          return "Purchase failed.";
    }

    return "Demo version";
}

juce::String IAPManager::getPrice() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return price;
}

juce::String IAPManager::getProductId() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return productId;
}

void IAPManager::handleBridgeEvent(std::int32_t bridgeState,
                                   const char* message,
                                   const char* displayPrice)
{
    const juce::String newMessage = message != nullptr ? juce::String::fromUTF8(message) : juce::String();
    const juce::String newPrice = displayPrice != nullptr ? juce::String::fromUTF8(displayPrice) : juce::String();

    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (newPrice.isNotEmpty())
            price = newPrice;
    }

    switch (bridgeState)
    {
        case 0:
            unlocked.store(false, std::memory_order_release);
            operationInFlight.store(false, std::memory_order_release);
            setState(State::Unavailable, newMessage);
            break;
        case 1:
            setState(State::Loading, newMessage);
            break;
        case 2:
            unlocked.store(false, std::memory_order_release);
            operationInFlight.store(false, std::memory_order_release);
            setState(State::Ready, newMessage);
            break;
        case 3:
            operationInFlight.store(true, std::memory_order_release);
            setState(State::Purchasing, newMessage);
            break;
        case 4:
            operationInFlight.store(true, std::memory_order_release);
            setState(State::Restoring, newMessage);
            break;
        case 5:
            unlocked.store(true, std::memory_order_release);
            operationInFlight.store(false, std::memory_order_release);
            setState(State::Unlocked);
            break;
        case 6:
            operationInFlight.store(false, std::memory_order_release);
            setState(State::Pending, newMessage);
            break;
        default:
            operationInFlight.store(false, std::memory_order_release);
            setState(isUnlocked() ? State::Unlocked : State::Failed, newMessage);
            break;
    }
}

IAPManager::Configuration IAPManager::configurationForName(const juce::String& name)
{
    if (name == product_config::productName)
        return { product_config::productId,
                 product_config::appGroupId,
                 product_config::appBundleId,
                 product_config::legacyPaidThroughVersion };

    return {};
}

void IAPManager::timerCallback()
{
    updateDemoAccess();
    syncSharedEntitlement();
}

void IAPManager::updateDemoAccess()
{
#if JUCE_IOS
    if (!configured.load(std::memory_order_acquire) || isUnlocked())
        return;

    juce::String configuredProductId;
    juce::String configuredBundleId;
    juce::String configuredGroupId;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        configuredProductId = productId;
        configuredBundleId = appBundleId;
        configuredGroupId = appGroupId;
    }

    std::int64_t secondsRemaining = 0;
    const auto result = soundrave_storekit_update_demo_access(
        configuredProductId.toRawUTF8(),
        configuredBundleId.toRawUTF8(),
        configuredGroupId.toRawUTF8(),
        &secondsRemaining);

    const bool known = result >= 0;
    const bool allowed = result == 1;
    const bool knownChanged = demoAccessKnown.exchange(known, std::memory_order_acq_rel) != known;
    const bool allowedChanged = demoAccessAllowed.exchange(allowed, std::memory_order_acq_rel) != allowed;
    const bool remainingChanged = demoSecondsRemaining.exchange(
        secondsRemaining, std::memory_order_acq_rel) != secondsRemaining;
    const bool changed = knownChanged || allowedChanged || remainingChanged;

    if (changed)
        sendChangeMessage();
#endif
}

void IAPManager::syncSharedEntitlement()
{
#if JUCE_IOS
    if (canManagePurchasesHere())
        return;

    juce::String configuredProductId;
    juce::String configuredBundleId;
    juce::String configuredGroupId;
    juce::String configuredLegacyVersion;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        configuredProductId = productId;
        configuredBundleId = appBundleId;
        configuredGroupId = appGroupId;
        configuredLegacyVersion = legacyPaidThroughVersion;
    }

    const auto sharedState = soundrave_storekit_read_shared_entitlement(
        configuredProductId.toRawUTF8(),
        configuredBundleId.toRawUTF8(),
        configuredGroupId.toRawUTF8(),
        configuredLegacyVersion.toRawUTF8());
    const bool nowUnlocked = sharedState == 1;
    const bool changed = unlocked.exchange(nowUnlocked, std::memory_order_acq_rel) != nowUnlocked;
    const auto nextState = nowUnlocked ? State::Unlocked
                                       : sharedState == 0 ? State::ExtensionLocked
                                                          : State::Unavailable;

    if (changed || getState() != nextState)
        setState(nextState);
#endif
}

void IAPManager::setState(State newState, const juce::String& message)
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        statusMessage = message;
    }

    state.store(static_cast<int>(newState), std::memory_order_release);
    sendChangeMessage();
}

bool IAPManager::isBusy() const noexcept
{
    const auto currentState = getState();
    return operationInFlight.load(std::memory_order_acquire)
        || currentState == State::Loading
        || currentState == State::Purchasing
        || currentState == State::Restoring;
}

} // namespace soundrave::iap
