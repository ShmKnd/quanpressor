#pragma once
#include <JuceHeader.h>

namespace soundrave::storage
{
inline juce::File productDirectory()
{
#if JUCE_MAC
    const auto base = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Library/Application Support");
#else
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#endif
    return base.getChildFile("SoundRave/Quanpressor");
}

inline juce::File legacyLibraryDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/SoundRave/Quanpressor");
}

inline juce::File legacyAudioDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Audio/Presets/SoundRave/Quanpressor");
}

inline juce::File migrateFile(const juce::File& target, const juce::Array<juce::File>& sources)
{
    juce::File source;
    for (const auto& candidate : sources)
        if (candidate != target && candidate.existsAsFile()) { source = candidate; break; }
    if (source == juce::File()) return target;
    juce::InterProcessLock lock("QuanpressorStorage-" + juce::String::toHexString(target.getFullPathName().hashCode64()));
    if (!lock.enter(5000)) return target.existsAsFile() ? target : source;
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock{lock};
    if (!target.existsAsFile())
    {
        if (!source.existsAsFile() || source.isSymbolicLink() || target.exists()
            || target.getParentDirectory().createDirectory().failed()) return source;
        juce::TemporaryFile temporary(target);
        if (!source.copyFileTo(temporary.getFile()) || !source.hasIdenticalContentTo(temporary.getFile())
            || !temporary.overwriteTargetFileWithTemporary()) return source;
    }
    if (!target.isSymbolicLink())
        for (const auto& candidate : sources)
        {
            if (candidate == target || candidate.isSymbolicLink() || !candidate.existsAsFile()) continue;
            const auto size = candidate.getSize();
            const auto modified = candidate.getLastModificationTime();
            if (candidate.hasIdenticalContentTo(target) && candidate.getSize() == size
                && candidate.getLastModificationTime() == modified)
            {
                const auto archive = target.getParentDirectory().getChildFile(".legacy-migrated");
                if (!archive.isSymbolicLink() && archive.createDirectory().wasOk())
                {
                    const auto backup = archive.getNonexistentChildFile(
                        candidate.getFileNameWithoutExtension(), candidate.getFileExtension(), false);
                    candidate.moveFileTo(backup);
                }
            }
        }
    return target;
}

inline juce::File migratePresets(const juce::File& legacy, const juce::File& target)
{
    const auto marker = target.getParentDirectory().getChildFile(".preset-migration-v1");
    if (marker.existsAsFile()) return target;
    juce::InterProcessLock lock("QuanpressorPresetMigration-" + juce::String::toHexString(target.getFullPathName().hashCode64()));
    if (!lock.enter(5000)) return legacy;
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock{lock};
    if (marker.existsAsFile()) return target;
    if (target.createDirectory().failed() || legacy.isSymbolicLink()) return legacy;
    if (legacy.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator(legacy, false, "*.xml", juce::File::findFiles, juce::File::FollowSymlinks::no))
        {
            const auto source = entry.getFile();
            if (source.isSymbolicLink()) return legacy;
            auto destination = target.getChildFile(source.getFileName());
            if (destination.existsAsFile() && !source.hasIdenticalContentTo(destination))
            {
                const auto stem = source.getFileNameWithoutExtension() + "-legacy";
                destination = target.getChildFile(stem + ".xml");
                for (int suffix = 2; destination.existsAsFile() && !source.hasIdenticalContentTo(destination); ++suffix)
                    destination = target.getChildFile(stem + "-" + juce::String(suffix) + ".xml");
            }
            if (!destination.existsAsFile())
            {
                juce::TemporaryFile temporary(destination);
                if (!source.copyFileTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary()) return legacy;
            }
        }
    if (!marker.replaceWithText("Preset migration complete\n")) return legacy;
    return target;
}
}
