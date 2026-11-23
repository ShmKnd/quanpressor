#include "../Source/ProductStorage.h"
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("QuanpressorStorageTest", "", false);
    struct Cleanup { juce::File root; ~Cleanup() { root.deleteRecursively(); } } cleanup { root };
    const auto legacy = root.getChildFile("legacy");
    const auto product = root.getChildFile("product");
    const auto presets = product.getChildFile("Presets");
    check(legacy.createDirectory().wasOk() && presets.createDirectory().wasOk(), "fixture directories");
    check(legacy.getChildFile("tone.xml").replaceWithText("old")
        && presets.getChildFile("tone.xml").replaceWithText("new"), "preset fixtures");
    check(soundrave::storage::migratePresets(legacy, presets) == presets, "preset migration");
    check(presets.getChildFile("tone.xml").loadFileAsString() == "new"
        && presets.getChildFile("tone-legacy.xml").loadFileAsString() == "old", "collision preserves both");
    check(legacy.getChildFile("tone.xml").existsAsFile(), "original preset preserved");
    check(presets.getChildFile("tone-legacy.xml").deleteFile(), "remove migrated preset");
    check(soundrave::storage::migratePresets(legacy, presets) == presets
        && !presets.getChildFile("tone-legacy.xml").exists(), "deleted preset stays deleted");
    const auto license = legacy.getChildFile("license");
    const auto installed = product.getChildFile("license");
    check(license.replaceWithText("valid data"), "license fixture");
    check(soundrave::storage::migrateFile(installed, { license }) == installed
        && license.existsAsFile() && installed.hasIdenticalContentTo(license), "license copy preserves original");
    check(installed.replaceWithText("current") &&
        soundrave::storage::migrateFile(installed, { license }) == installed &&
        installed.loadFileAsString() == "current", "existing license state preserved");
    const auto newerState = legacy.getChildFile("newer-state");
    const auto olderState = legacy.getChildFile("older-state");
    const auto selectedState = product.getChildFile("state");
    check(newerState.replaceWithText("newer") && olderState.replaceWithText("older"), "state fixtures");
    check(soundrave::storage::migrateFile(selectedState, { newerState, olderState }) == selectedState
        && selectedState.loadFileAsString() == "newer", "legacy state priority");
    const auto blocked = root.getChildFile("blocked");
    check(blocked.replaceWithText("file"), "blocked fixture");
    check(soundrave::storage::migrateFile(blocked.getChildFile("license"), { license }) == license,
        "failed migration keeps old license available");
    std::cout << "PASS: preset and license migration\n";
}
