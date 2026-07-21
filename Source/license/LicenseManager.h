#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <mutex>

class LicenseManager final
{
public:
    enum class State
    {
        Unlicensed,
        Valid,
        Expired,
        Invalid,
        TrialActive,
        TrialExpired
    };

    struct LicenseInfo
    {
        juce::String product;
        juce::String licenseId;
        juce::String user;
        juce::String type;
        juce::String expiry;
        juce::String version;
        State state = State::Unlicensed;
    };

    static LicenseManager& getInstance();

    void initialise(const juce::String& productName, int trialDays = 14,
                    const juce::StringArray& additionalAcceptedProducts = {});
    bool loadFromFile(const juce::File& file,
                      const juce::StringArray* allowedProducts = nullptr);

    State getState() const noexcept;
    LicenseInfo getInfo() const;
    juce::String getStatusString() const;
    juce::String getExpiryString() const;
    juce::String getProductName() const;
    int getTrialDaysRemaining() const;
    bool isAudioRestricted() const noexcept;

    static juce::File getProductDataDirectory(const juce::String& productName);
    static juce::File getDefaultLicenseFile(const juce::String& productName);

private:
    LicenseManager() = default;

    bool verifySignature(const juce::var& payload,
                         const juce::String& signatureBase64) const;
    void initialiseTrial();
    State calculateTrialState() const;
    void setState(State newState);

    static constexpr const char* publicKeyBase64 =
        "ttbXpojljSaQlCvv9Zduop7VBK54s+Y6I/UyeN6d7Hk=";

    mutable std::mutex mutex;
    LicenseInfo info;
    juce::String productName;
    juce::StringArray acceptedProducts;
    int trialDays = 14;
    std::unique_ptr<juce::PropertiesFile> properties;
    std::atomic<int> state { static_cast<int>(State::Unlicensed) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseManager)
};
