#pragma once

#include <array>
#include <iterator>

struct QuanpressorPresetValue
{
    const char* parameterId;
    float value;
};

enum class QuanpressorPresetCategory
{
    Fet = 0,
    Opto,
    Vca,
    VariMu,
    Sidechain
};

struct QuanpressorFactoryPreset
{
    const char* name;
    QuanpressorPresetCategory category;
    const QuanpressorPresetValue* values;
    int valueCount;
};

static constexpr std::array<const char*, 5> quanpressorPresetCategoryNames {{
    "FET",
    "OPTO",
    "VCA",
    "VARI-MU",
    "SIDECHAIN"
}};

static constexpr QuanpressorPresetValue fetVocalPunchValues[] {
    { "mode", 0.0f },
    { "threshold", -20.0f },
    { "ratio", 4.0f },
    { "attack", 30.0f },
    { "release", 260.0f },
    { "drive", 2.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue fetDrumPunchValues[] {
    { "mode", 0.0f },
    { "threshold", -18.0f },
    { "ratio", 4.0f },
    { "attack", 70.0f },
    { "release", 110.0f },
    { "drive", 3.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue fetParallelSmashValues[] {
    { "mode", 0.0f },
    { "threshold", -32.0f },
    { "ratio", 20.0f },
    { "attack", 10.0f },
    { "release", 75.0f },
    { "drive", 8.0f },
    { "mix", 35.0f }
};

static constexpr QuanpressorPresetValue optoVocalLevelerValues[] {
    { "mode", 1.0f },
    { "threshold", -22.0f },
    { "ratio", 4.0f },
    { "attack", 15.0f },
    { "release", 500.0f },
    { "drive", 1.5f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue optoBassLevelerValues[] {
    { "mode", 1.0f },
    { "threshold", -20.0f },
    { "ratio", 5.0f },
    { "attack", 25.0f },
    { "release", 650.0f },
    { "drive", 2.0f },
    { "lowFreq", 90.0f },
    { "lowAmt", 0.12f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue optoGentleLevelValues[] {
    { "mode", 1.0f },
    { "threshold", -16.0f },
    { "ratio", 2.5f },
    { "attack", 35.0f },
    { "release", 800.0f },
    { "drive", 0.8f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue vcaBusGlueValues[] {
    { "mode", 2.0f },
    { "threshold", -14.0f },
    { "ratio", 2.0f },
    { "attack", 30.0f },
    { "release", 150.0f },
    { "knee", 6.0f },
    { "autoRelease", 1.0f },
    { "drive", 1.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue vcaDrumControlValues[] {
    { "mode", 2.0f },
    { "threshold", -18.0f },
    { "ratio", 4.0f },
    { "attack", 20.0f },
    { "release", 100.0f },
    { "knee", 3.0f },
    { "drive", 2.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue variMuMixGlueValues[] {
    { "mode", 3.0f },
    { "threshold", -14.0f },
    { "ratio", 2.5f },
    { "attack", 25.0f },
    { "release", 400.0f },
    { "knee", 9.0f },
    { "drive", 2.0f },
    { "muSpeed", 2.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue variMuVocalWarmthValues[] {
    { "mode", 3.0f },
    { "threshold", -18.0f },
    { "ratio", 3.0f },
    { "attack", 15.0f },
    { "release", 600.0f },
    { "knee", 10.0f },
    { "drive", 4.0f },
    { "muSpeed", 1.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue variMuMasterGentleValues[] {
    { "mode", 3.0f },
    { "threshold", -10.0f },
    { "ratio", 1.5f },
    { "attack", 40.0f },
    { "release", 800.0f },
    { "knee", 12.0f },
    { "drive", 1.0f },
    { "muSpeed", 4.0f },
    { "highFreq", 10000.0f },
    { "highAmt", 0.08f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue vcaSidechainDuckValues[] {
    { "mode", 2.0f },
    { "externalSidechain", 1.0f },
    { "threshold", -24.0f },
    { "ratio", 8.0f },
    { "attack", 1.0f },
    { "release", 250.0f },
    { "knee", 6.0f },
    { "mix", 100.0f }
};

static constexpr QuanpressorPresetValue fetSidechainPumpValues[] {
    { "mode", 0.0f },
    { "externalSidechain", 1.0f },
    { "threshold", -28.0f },
    { "ratio", 8.0f },
    { "attack", 5.0f },
    { "release", 380.0f },
    { "mix", 100.0f }
};

#define QUANPRESSOR_PRESET(name, category, values) \
    { name, category, values, static_cast<int>(std::size(values)) }

static constexpr QuanpressorFactoryPreset quanpressorFactoryPresets[] {
    { "INIT - Quanpressor", QuanpressorPresetCategory::Fet, nullptr, 0 },
    QUANPRESSOR_PRESET("FET - Vocal Punch",
                       QuanpressorPresetCategory::Fet, fetVocalPunchValues),
    QUANPRESSOR_PRESET("FET - Drum Punch",
                       QuanpressorPresetCategory::Fet, fetDrumPunchValues),
    QUANPRESSOR_PRESET("FET - Parallel Smash",
                       QuanpressorPresetCategory::Fet, fetParallelSmashValues),
    QUANPRESSOR_PRESET("OPTO - Vocal Leveler",
                       QuanpressorPresetCategory::Opto, optoVocalLevelerValues),
    QUANPRESSOR_PRESET("OPTO - Bass Leveler",
                       QuanpressorPresetCategory::Opto, optoBassLevelerValues),
    QUANPRESSOR_PRESET("OPTO - Gentle Level",
                       QuanpressorPresetCategory::Opto, optoGentleLevelValues),
    QUANPRESSOR_PRESET("VCA - Bus Glue",
                       QuanpressorPresetCategory::Vca, vcaBusGlueValues),
    QUANPRESSOR_PRESET("VCA - Drum Control",
                       QuanpressorPresetCategory::Vca, vcaDrumControlValues),
    QUANPRESSOR_PRESET("Vari-Mu - Mix Glue",
                       QuanpressorPresetCategory::VariMu, variMuMixGlueValues),
    QUANPRESSOR_PRESET("Vari-Mu - Vocal Warmth",
                       QuanpressorPresetCategory::VariMu, variMuVocalWarmthValues),
    QUANPRESSOR_PRESET("Vari-Mu - Master Gentle",
                       QuanpressorPresetCategory::VariMu, variMuMasterGentleValues),
    QUANPRESSOR_PRESET("SC - VCA Duck",
                       QuanpressorPresetCategory::Sidechain, vcaSidechainDuckValues),
    QUANPRESSOR_PRESET("SC - FET Pump",
                       QuanpressorPresetCategory::Sidechain, fetSidechainPumpValues)
};

#undef QUANPRESSOR_PRESET

static constexpr int quanpressorFactoryPresetCount =
    static_cast<int>(std::size(quanpressorFactoryPresets));
