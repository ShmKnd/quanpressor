#include <JuceHeader.h>

#if (JUCE_MAC || JUCE_WINDOWS) && JUCE_STANDALONE_APPLICATION
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "ProductStorage.h"

namespace
{
class QuanpressorStandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Quanpressor"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        const auto legacy = options.getDefaultFile();
        options.folderName = "SoundRave/Quanpressor";
        const auto target = options.getDefaultFile();
        if (soundrave::storage::migrateFile(target, { legacy }) == legacy && legacy != target)
            options.folderName = {};
        properties.setStorageParameters(options);
        auto holder = std::make_unique<juce::StandalonePluginHolder>(properties.getUserSettings(), false,
            juce::String(), nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts>(), false);
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            headlessHolder = std::move(holder);
        else
        {
            window = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(),
                juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                std::move(holder));
            window->setVisible(true);
        }
    }

    void systemRequestedQuit() override
    {
        if (headlessHolder != nullptr) headlessHolder->savePluginState();
        if (window != nullptr) window->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay(100, [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        else quit();
    }

    void shutdown() override
    {
        headlessHolder.reset();
        window.reset();
        properties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandalonePluginHolder> headlessHolder;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};
}
JUCE_CREATE_APPLICATION_DEFINE(QuanpressorStandaloneApp)
#endif
