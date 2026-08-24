#pragma once

// -----------------------------------------------------------------------------
// SilverSettings - the schema behind the settings palette.
//
// Every row the user sees is declared here and bound to a dotted config path.
// The panel renders itself from this list, so adding a setting means adding one
// entry - no UI code changes. Reading, writing, revert-to-default and live
// reload all go through SilverConfig, which means the palette and the JSON file
// are always two views of the same state.
// -----------------------------------------------------------------------------

#include "silver_config.h"
#include <string>
#include <vector>

enum SettingKind {
    SETTING_TOGGLE = 0,   // on/off switch
    SETTING_STEPPER,      // integer with - / + buttons
    SETTING_SLIDER,       // continuous float
    SETTING_CHOICE        // one of a fixed list of strings
};

struct SettingChoice {
    const char* value;    // written to the config
    const char* label;    // shown in the dropdown
};

struct SettingSpec {
    const char* section;
    const char* path;
    const char* label;
    const char* description;
    SettingKind kind;
    float min = 0.0f;
    float max = 1.0f;
    float step = 1.0f;
    std::vector<SettingChoice> choices;
};

// The catalogue. Order defines the order in the panel.
inline const std::vector<SettingSpec>& settingsSchema() {
    static const std::vector<SettingSpec> schema = {
        // ---- Appearance ------------------------------------------------------
        { "Appearance", "text.pixelSize", "Font Size",
          "Size of all interface text. The rest of the interface scales with it.",
          SETTING_STEPPER, 12, 24, 1 },

        { "Appearance", "text.hinting", "Font Hinting",
          "How glyph stems are fitted to the pixel grid. Light matches GTK and Qt.",
          SETTING_CHOICE, 0, 0, 0,
          { { "light", "Light" }, { "normal", "Normal" }, { "none", "None" }, { "mono", "Monochrome" } } },

        { "Appearance", "icons.strokeScale", "Icon Weight",
          "Thickness of icon strokes. Raise it if icons look too light next to text.",
          SETTING_SLIDER, 0.75f, 2.0f, 0.05f },

        { "Appearance", "grid.tileRadius", "Rounded Corners",
          "Corner rounding applied to photo tiles and cards.",
          SETTING_SLIDER, 0.0f, 24.0f, 1.0f },

        { "Appearance", "grid.gap", "Grid Spacing",
          "Space between photos in the grid.",
          SETTING_SLIDER, 0.0f, 40.0f, 1.0f },

        { "Appearance", "grid.sidePadding", "Grid Margin",
          "Space between the grid and the window edges.",
          SETTING_SLIDER, 0.0f, 80.0f, 1.0f },

        { "Appearance", "layout.sidebarWidth", "Sidebar Width",
          "Width of the preview and info panel.",
          SETTING_SLIDER, 240.0f, 560.0f, 10.0f },

        // ---- Motion ----------------------------------------------------------
        { "Motion", "animation.enabled", "Animations",
          "Applies animation to scrolling, popups, selection and layout changes.",
          SETTING_TOGGLE },

        { "Motion", "animation.model", "Motion Model",
          "Spring uses second-order physics; exponential uses plain smoothing.",
          SETTING_CHOICE, 0, 0, 0,
          { { "spring", "Spring" }, { "exponential", "Exponential" } } },

        { "Motion", "animation.speedMultiplier", "Animation Speed",
          "Scales how quickly every transition settles.",
          SETTING_SLIDER, 0.25f, 3.0f, 0.05f },

        { "Motion", "animation.layoutMargin", "Layout Animation Range",
          "How far beyond the visible area tiles keep animating during a reflow.",
          SETTING_SLIDER, 0.0f, 2000.0f, 50.0f },

        { "Motion", "animation.animateOffscreenTiles", "Animate Off-screen Tiles",
          "Glides tiles outside the viewport too. Costs performance in large libraries.",
          SETTING_TOGGLE },

        // ---- Thumbnails ------------------------------------------------------
        { "Thumbnails", "thumbnails.diskCache", "Disk Cache",
          "Stores generated thumbnails on disk so they survive restarts.",
          SETTING_TOGGLE },

        { "Thumbnails", "thumbnails.useSharedCache", "Use Desktop Thumbnails",
          "Reuses thumbnails your file manager already generated.",
          SETTING_TOGGLE },

        { "Thumbnails", "thumbnails.prewarmLibrary", "Pre-generate Library",
          "Builds the whole library's thumbnail cache quietly in the background.",
          SETTING_TOGGLE },

        { "Thumbnails", "thumbnails.reservedWorkers", "Reserved Workers",
          "Threads kept free for on-screen photos while pre-generation runs.",
          SETTING_STEPPER, 1, 8, 1 },

        { "Thumbnails", "thumbnails.uploadsPerFrame", "Uploads Per Frame",
          "How many decoded thumbnails reach the GPU each frame.",
          SETTING_STEPPER, 4, 128, 4 },

        { "Thumbnails", "thumbnails.maxResidentMegabytes", "Texture Memory",
          "GPU memory budget for thumbnails before the least recently seen are released.",
          SETTING_STEPPER, 64, 1024, 32 },

        { "Thumbnails", "thumbnails.startupPrimeCount", "Startup Preload",
          "Photos decoded before the window is first shown.",
          SETTING_STEPPER, 0, 400, 20 },

        { "Thumbnails", "gif.animate", "Animate GIFs",
          "Plays animated GIFs in the viewer instead of showing the first frame.",
          SETTING_TOGGLE },

        // ---- Library ---------------------------------------------------------
        { "Library", "scanner.pruneMissing", "Remove Deleted Photos",
          "Drops photos from the index when their files disappear.",
          SETTING_TOGGLE },

        { "Library", "scanner.batchSize", "Index Batch Size",
          "How many photos are written to the index per transaction.",
          SETTING_STEPPER, 50, 2000, 50 },

        { "Library", "scanner.refreshIntervalSeconds", "Refresh Interval",
          "How often the grid folds in newly indexed photos while scanning.",
          SETTING_SLIDER, 0.1f, 5.0f, 0.1f },

        // ---- Keyboard --------------------------------------------------------
        { "Keyboard", "keys.capsLockActsAsEscape", "Caps Lock Acts As Escape",
          "Enable if your compositor remaps Caps Lock to Escape.",
          SETTING_TOGGLE },

        { "Keyboard", "keys.escapeRepeat", "Escape Auto-repeat",
          "Lets a held Escape unwind several layers instead of one per press.",
          SETTING_TOGGLE },
    };
    return schema;
}

// ---- value access helpers ---------------------------------------------------

inline float settingNumber(const SettingSpec& spec) {
    return SilverConfig::get().num(spec.path, spec.min);
}

inline bool settingFlag(const SettingSpec& spec) {
    return SilverConfig::get().flag(spec.path, false);
}

inline std::string settingText(const SettingSpec& spec) {
    const char* fallback = spec.choices.empty() ? "" : spec.choices[0].value;
    return SilverConfig::get().text(spec.path, fallback);
}

// Human-readable current value, for the collapsed control.
inline std::string settingDisplayValue(const SettingSpec& spec) {
    switch (spec.kind) {
        case SETTING_TOGGLE:
            return settingFlag(spec) ? "On" : "Off";
        case SETTING_STEPPER: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", (int)std::lround(settingNumber(spec)));
            return buf;
        }
        case SETTING_SLIDER: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", settingNumber(spec));
            return buf;
        }
        case SETTING_CHOICE: {
            std::string value = settingText(spec);
            for (const SettingChoice& c : spec.choices) {
                if (value == c.value) return c.label;
            }
            return value;
        }
    }
    return "";
}

inline int settingChoiceIndex(const SettingSpec& spec) {
    std::string value = settingText(spec);
    for (size_t i = 0; i < spec.choices.size(); ++i) {
        if (value == spec.choices[i].value) return (int)i;
    }
    return 0;
}

// ---- mutation ---------------------------------------------------------------

inline void settingSetNumber(const SettingSpec& spec, float value) {
    value = std::max(spec.min, std::min(spec.max, value));
    SilverConfig::get().setNum(spec.path, value);
}

inline void settingNudge(const SettingSpec& spec, int direction) {
    float step = (spec.step > 0.0f) ? spec.step : 1.0f;
    settingSetNumber(spec, settingNumber(spec) + step * (float)direction);
}

inline void settingToggle(const SettingSpec& spec) {
    SilverConfig::get().setFlag(spec.path, !settingFlag(spec));
}

inline void settingSetChoice(const SettingSpec& spec, int index) {
    if (spec.choices.empty()) return;
    if (index < 0) index = (int)spec.choices.size() - 1;
    if (index >= (int)spec.choices.size()) index = 0;
    SilverConfig::get().setText(spec.path, spec.choices[(size_t)index].value);
}

// Activating a row: toggles switch, cycles choice. Numbers use left/right.
inline void settingActivate(const SettingSpec& spec) {
    if (spec.kind == SETTING_TOGGLE) settingToggle(spec);
    else if (spec.kind == SETTING_CHOICE) settingSetChoice(spec, settingChoiceIndex(spec) + 1);
}

inline bool settingModified(const SettingSpec& spec) {
    return SilverConfig::get().isModified(spec.path);
}

// Case-insensitive substring match over label, description and section.
inline bool settingMatches(const SettingSpec& spec, const std::string& queryLower) {
    if (queryLower.empty()) return true;

    auto contains = [&](const char* text) {
        if (!text) return false;
        std::string hay(text);
        for (char& c : hay) c = (char)tolower((unsigned char)c);
        return hay.find(queryLower) != std::string::npos;
    };
    return contains(spec.label) || contains(spec.description) || contains(spec.section);
}
