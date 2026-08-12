#include "LicenseManager.h"
#include <sodium.h>

namespace
{
constexpr const char* trialStartKey = "trial_start_ms";
constexpr const char* trialLastSeenKey = "trial_last_seen_ms";

juce::String serialiseCanonicalJson(const juce::var& root)
{
    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return {};

    juce::StringArray keys;
    for (const auto& property : object->getProperties())
    {
        const auto key = property.name.toString();
        if (key != "signature")
            keys.add(key);
    }
    keys.sort(false);

    juce::String result("{");
    for (int index = 0; index < keys.size(); ++index)
    {
        if (index != 0)
            result += ",";

        const auto& key = keys[index];
        result += juce::JSON::toString(juce::var(key), true);
        result += ":";
        result += juce::JSON::toString(object->getProperty(key), true);
    }
    result += "}";
    return result;
}
}

LicenseManager& LicenseManager::getInstance()
{
    static LicenseManager instance;
    return instance;
}

void LicenseManager::initialise(const juce::String& requestedProduct, int requestedTrialDays,
                                const juce::StringArray& additionalAcceptedProducts)
{
    productName = requestedProduct.trim();
    trialDays = juce::jmax(1, requestedTrialDays);
    acceptedProducts.clear();
    acceptedProducts.add(productName);
    for (const auto& alias : additionalAcceptedProducts)
        if (alias.isNotEmpty())
            acceptedProducts.addIfNotAlreadyThere(alias);

    juce::PropertiesFile::Options options;
    options.applicationName = "license_state";
    options.filenameSuffix = ".props";
    options.commonToAllUsers = false;
    properties = std::make_unique<juce::PropertiesFile>(
        getProductDataDirectory(productName)
            .getChildFile(".license_state.props"),
        options);

    if (sodium_init() < 0)
    {
        setState(State::Invalid);
        return;
    }

    for (const auto& candidate : acceptedProducts)
    {
        const auto file = getDefaultLicenseFile(candidate);
        if (file.existsAsFile() && loadFromFile(file, &acceptedProducts))
            return;
    }

    initialiseTrial();
}

bool LicenseManager::loadFromFile(const juce::File& file,
                                  const juce::StringArray* allowedProducts)
{
    const auto root = juce::JSON::parse(file.loadFileAsString());
    auto* object = root.getDynamicObject();
    if (!file.existsAsFile() || object == nullptr)
    {
        setState(State::Invalid);
        return false;
    }

    const auto signature = root["signature"].toString();
    const auto product = root["product"].toString();
    const auto& products = allowedProducts != nullptr ? *allowedProducts : acceptedProducts;
    if (signature.isEmpty() || !products.contains(product) || !verifySignature(root, signature))
    {
        setState(State::Invalid);
        return false;
    }

    LicenseInfo newInfo;
    newInfo.product = product;
    newInfo.licenseId = root["license_id"].toString();
    newInfo.user = root["user"].toString();
    newInfo.type = root["type"].toString();
    newInfo.expiry = root["expiry"].toString();
    newInfo.version = root["version"].toString();
    newInfo.state = State::Valid;

    if (newInfo.expiry.isNotEmpty())
    {
        const auto expiryTime = juce::Time::fromISO8601(newInfo.expiry + "T23:59:59Z");
        if (expiryTime.toMilliseconds() <= 0
            || juce::Time::getCurrentTime() > expiryTime)
            newInfo.state = State::Expired;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex);
        info = newInfo;
    }
    state.store(static_cast<int>(newInfo.state), std::memory_order_release);
    return true;
}

LicenseManager::State LicenseManager::getState() const noexcept
{
    return static_cast<State>(state.load(std::memory_order_acquire));
}

LicenseManager::LicenseInfo LicenseManager::getInfo() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return info;
}

juce::String LicenseManager::getProductName() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return productName;
}

juce::String LicenseManager::getStatusString() const
{
    switch (getState())
    {
        case State::Valid:        return "Licensed";
        case State::Expired:      return "License expired";
        case State::Invalid:      return "Invalid license";
        case State::TrialActive:  return "Trial - " + juce::String(getTrialDaysRemaining()) + " days left";
        case State::TrialExpired: return "Trial expired";
        case State::Unlicensed:   return "Unlicensed";
    }
    return "Unlicensed";
}

juce::String LicenseManager::getExpiryString() const
{
    const auto currentInfo = getInfo();
    if (currentInfo.expiry.isNotEmpty())
        return currentInfo.expiry;

    if (properties != nullptr && properties->containsKey(trialStartKey))
    {
        const auto startMs = properties->getValue(trialStartKey).getLargeIntValue();
        return juce::Time(startMs + static_cast<juce::int64>(trialDays) * 86400000LL)
            .toString(true, false);
    }
    return "Never";
}

int LicenseManager::getTrialDaysRemaining() const
{
    if (properties == nullptr || !properties->containsKey(trialStartKey))
        return trialDays;

    const auto startMs = properties->getValue(trialStartKey).getLargeIntValue();
    const auto remainingMs = startMs + static_cast<juce::int64>(trialDays) * 86400000LL
                           - juce::Time::currentTimeMillis();
    if (remainingMs <= 0)
        return 0;
    return static_cast<int>((remainingMs + 86399999LL) / 86400000LL);
}

bool LicenseManager::isAudioRestricted() const noexcept
{
    const auto current = getState();
    return current == State::Expired || current == State::Invalid
        || current == State::TrialExpired;
}

bool LicenseManager::verifySignature(const juce::var& root,
                                     const juce::String& signatureBase64) const
{
    juce::MemoryBlock publicKey;
    {
        juce::MemoryOutputStream publicKeyStream(publicKey, false);
        if (!juce::Base64::convertFromBase64(publicKeyStream, publicKeyBase64))
            return false;
    }
    if (publicKey.getSize() != crypto_sign_PUBLICKEYBYTES)
        return false;

    juce::MemoryBlock signature;
    {
        juce::MemoryOutputStream signatureStream(signature, false);
        if (!juce::Base64::convertFromBase64(signatureStream, signatureBase64))
            return false;
    }
    if (signature.getSize() != crypto_sign_BYTES)
        return false;

    const auto canonicalJson = serialiseCanonicalJson(root);
    if (canonicalJson.isEmpty())
        return false;

    const auto utf8 = canonicalJson.toUTF8();
    return crypto_sign_verify_detached(
               static_cast<const unsigned char*>(signature.getData()),
               reinterpret_cast<const unsigned char*>(utf8.getAddress()),
               static_cast<unsigned long long>(canonicalJson.getNumBytesAsUTF8()),
               static_cast<const unsigned char*>(publicKey.getData())) == 0;
}

void LicenseManager::initialiseTrial()
{
    if (properties == nullptr)
    {
        setState(State::Unlicensed);
        return;
    }

    const auto nowMs = juce::Time::currentTimeMillis();
    if (!properties->containsKey(trialStartKey))
        properties->setValue(trialStartKey, juce::String(nowMs));

    const auto trialState = calculateTrialState();
    if (trialState == State::TrialActive)
        properties->setValue(trialLastSeenKey, juce::String(nowMs));
    properties->saveIfNeeded();
    setState(trialState);
}

LicenseManager::State LicenseManager::calculateTrialState() const
{
    if (properties == nullptr)
        return State::Unlicensed;

    const auto nowMs = juce::Time::currentTimeMillis();
    const auto startMs = properties->getValue(trialStartKey).getLargeIntValue();
    const auto lastSeenMs = properties->getValue(trialLastSeenKey).getLargeIntValue();
    constexpr juce::int64 clockRollbackToleranceMs = 6LL * 60LL * 60LL * 1000LL;
    if (startMs <= 0 || nowMs + clockRollbackToleranceMs < lastSeenMs)
        return State::TrialExpired;

    return nowMs - startMs < static_cast<juce::int64>(trialDays) * 86400000LL
        ? State::TrialActive : State::TrialExpired;
}

void LicenseManager::setState(State newState)
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        info.state = newState;
    }
    state.store(static_cast<int>(newState), std::memory_order_release);
}

juce::File LicenseManager::getProductDataDirectory(const juce::String& product)
{
#if JUCE_MAC || JUCE_WINDOWS
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#else
    auto base = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile(".config");
#endif

    // Keep every SoundRave product isolated in its own subdirectory.
    // macOS:  ~/Library/Application Support/SoundRave/<Product>/
    // Windows: %APPDATA%\\SoundRave\\<Product>\\ (directory)
    auto directory = base.getChildFile("SoundRave").getChildFile(product);
    directory.createDirectory();
    return directory;
}

juce::File LicenseManager::getDefaultLicenseFile(const juce::String& product)
{
    return getProductDataDirectory(product).getChildFile(product + ".soundravelicense");
}
