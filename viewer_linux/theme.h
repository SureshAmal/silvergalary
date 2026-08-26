#pragma once

// ThemeManager owns *which* theme is active and whether it is showing its dark
// or light variant. The themes themselves - all their colour, shape, motion and
// typography tokens - live in silver_theme.h.
//
// theme.current stays the flat palette the UI has always read, derived from the
// active theme's tokens. New code should prefer theme.tokens() for shape,
// motion and typography, which the flat palette does not carry.

#include "silver_theme.h"
#include "silver_config.h"
#include <stdint.h>
#include <string>
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <atomic>
#include <sys/stat.h>
#include <algorithm>

enum ThemeMode {
    THEME_SYSTEM = 0,
    THEME_DARK = 1,
    THEME_LIGHT = 2
};

class ThemeManager {
public:
    std::atomic<ThemeMode> mode{THEME_SYSTEM};
    std::atomic<bool> isDarkMode{true};
    time_t lastDconfMtime = 0;
    float pollTimer = 0.0f;

    ThemePalette current;          // derived; what the existing UI reads
    ThemeTokens active;            // full token set for the active variant
    std::string themeId = "material-expressive";

    const ThemeTokens& tokens() const { return active; }

    // ------------------------------------------------------------------
    // Theme selection
    // ------------------------------------------------------------------
    std::string themeLabel() const {
        const ThemeDefinition* def = ThemeRegistry::get().find(themeId);
        return def ? def->label : themeId;
    }

    static std::vector<std::string> availableThemes() { return ThemeRegistry::get().ids(); }

    void setTheme(const std::string& id) {
        if (!ThemeRegistry::get().find(id)) return;
        themeId = id;
        updatePalette();
    }

    void cycleTheme() {
        const std::vector<ThemeDefinition>& all = ThemeRegistry::get().all();
        if (all.empty()) return;
        size_t idx = 0;
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].id == themeId) { idx = i; break; }
        setTheme(all[(idx + 1) % all.size()].id);
    }

    // ------------------------------------------------------------------
    // System dark/light detection
    // ------------------------------------------------------------------
    static bool detectSystemDark() {
        const char* home = getenv("HOME");
        if (home) {
            std::string dconfPath = std::string(home) + "/.config/dconf/user";
            FILE* fp = fopen(dconfPath.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                std::string s;
                if (sz > 0 && sz < 2 * 1024 * 1024) {
                    std::vector<char> buf(sz + 1, 0);
                    size_t readBytes = fread(buf.data(), 1, sz, fp);
                    s.assign(buf.data(), readBytes);
                }
                // Close on every path. The old code only closed inside the size
                // check, so an empty or oversized dconf file leaked a descriptor
                // every time the theme was re-detected - which happens on a poll.
                fclose(fp);

                if (!s.empty()) {
                    // 1. GNOME color-scheme ('prefer-dark' vs 'prefer-light' / 'default')
                    size_t pos = s.find("color-scheme");
                    if (pos != std::string::npos) {
                        std::string sub = s.substr(pos, std::min((size_t)100, s.size() - pos));
                        if (sub.find("prefer-dark") != std::string::npos) return true;
                        if (sub.find("prefer-light") != std::string::npos ||
                            sub.find("default") != std::string::npos) return false;
                    }

                    // 2. GTK theme name containing 'dark'
                    size_t gtkPos = s.find("gtk-theme");
                    if (gtkPos != std::string::npos) {
                        std::string gtkSub = s.substr(gtkPos, std::min((size_t)100, s.size() - gtkPos));
                        if (gtkSub.find("dark") != std::string::npos ||
                            gtkSub.find("Dark") != std::string::npos) return true;
                        if (gtkSub.find("light") != std::string::npos ||
                            gtkSub.find("Light") != std::string::npos) return false;
                    }
                }
            }
        }

        // 3. Environment variables
        const char* gtkTheme = getenv("GTK_THEME");
        if (gtkTheme) {
            std::string gt(gtkTheme);
            if (gt.find("dark") != std::string::npos || gt.find("Dark") != std::string::npos) return true;
            if (gt.find("light") != std::string::npos || gt.find("Light") != std::string::npos) return false;
        }

        return true; // Default to dark
    }

    void setThemeMode(ThemeMode m) {
        mode = m;
        if (m == THEME_DARK)       isDarkMode = true;
        else if (m == THEME_LIGHT) isDarkMode = false;
        else                       isDarkMode = detectSystemDark();
        updatePalette();
    }

    void cycleThemeMode() {
        ThemeMode cur = mode.load();
        if (cur == THEME_SYSTEM)    setThemeMode(THEME_DARK);
        else if (cur == THEME_DARK) setThemeMode(THEME_LIGHT);
        else                        setThemeMode(THEME_SYSTEM);
    }

    void toggleManual() { cycleThemeMode(); }

    std::string getThemeModeName() const {
        switch (mode.load()) {
            case THEME_DARK:   return "Dark";
            case THEME_LIGHT:  return "Light";
            case THEME_SYSTEM: return isDarkMode.load() ? "System (Dark)" : "System (Light)";
        }
        return "System";
    }

    void applyConfig() {
        const SilverConfig& c = SilverConfig::get();
        std::string wanted = c.text("theme.name", themeId.c_str());
        if (ThemeRegistry::get().find(wanted)) themeId = wanted;

        std::string modeName = c.text("theme.mode", "system");
        if (modeName == "dark")       mode = THEME_DARK;
        else if (modeName == "light") mode = THEME_LIGHT;
        else                          mode = THEME_SYSTEM;

        if (mode.load() == THEME_SYSTEM) isDarkMode = detectSystemDark();
        else                             isDarkMode = (mode.load() == THEME_DARK);
        updatePalette();
    }

    void init() {
        applyConfig();

        const char* home = getenv("HOME");
        if (home) {
            std::string dconfPath = std::string(home) + "/.config/dconf/user";
            struct stat st;
            if (stat(dconfPath.c_str(), &st) == 0) lastDconfMtime = st.st_mtime;
        }
    }

    void update(float dt) {
        if (mode.load() != THEME_SYSTEM) return;

        pollTimer += dt;
        if (pollTimer < 0.20f) return;   // stat() 5x a second is far cheaper than a re-parse
        pollTimer = 0.0f;

        const char* home = getenv("HOME");
        if (!home) return;
        std::string dconfPath = std::string(home) + "/.config/dconf/user";
        struct stat st;
        if (stat(dconfPath.c_str(), &st) != 0) return;
        if (st.st_mtime == lastDconfMtime) return;

        lastDconfMtime = st.st_mtime;
        bool sysDark = detectSystemDark();
        if (sysDark != isDarkMode.load()) {
            isDarkMode = sysDark;
            updatePalette();
        }
    }

    // Resolve a config radius that may be either a number ("10") or the name of
    // a shape token ("medium"). Themes can then restyle every corner in the app
    // without the config having to repeat their numbers.
    float shapeToken(const char* name, float fallback) const {
        const ThemeShape& s = active.shape;
        if (!name || !*name) return fallback;
        if (!strcmp(name, "none"))                return s.none;
        if (!strcmp(name, "extraSmall"))          return s.extraSmall;
        if (!strcmp(name, "small"))               return s.small;
        if (!strcmp(name, "medium"))              return s.medium;
        if (!strcmp(name, "large"))               return s.large;
        if (!strcmp(name, "largeIncreased"))      return s.largeIncreased;
        if (!strcmp(name, "extraLarge"))          return s.extraLarge;
        if (!strcmp(name, "extraLargeIncreased")) return s.extraLargeIncreased;
        if (!strcmp(name, "extraExtraLarge"))     return s.extraExtraLarge;
        if (!strcmp(name, "full"))                return s.full;
        return fallback;
    }

    // Expressive morphs a container's corners while it is pressed. `pressed` is
    // the 0..1 press animation already driven by the spring system, so the shape
    // change rides the existing animation rather than adding another one.
    float pressedRadius(float restRadius, float pressed) const {
        return active.shape.morph(restRadius, pressed);
    }

    // Config value at `path` is either a number (used as-is) or a shape token
    // name. `fallback` is the shape tier to use when the key is missing.
    float radius(const char* path, const char* fallbackToken) const {
        const SilverConfig& c = SilverConfig::get();
        std::string asText = c.text(path, "");
        if (!asText.empty()) return shapeToken(asText.c_str(), shapeToken(fallbackToken, 0.0f));
        float dflt = shapeToken(fallbackToken, 0.0f);
        return c.num(path, dflt);
    }

    void updatePalette() {
        const ThemeRegistry& reg = ThemeRegistry::get();
        const ThemeDefinition* def = reg.find(themeId);
        if (!def) def = &reg.defaultTheme();

        active = def->variant(isDarkMode.load());
        applyOverrides(active);
        current = paletteFromTokens(active);
    }

private:
    // "#RRGGBB", "#RRGGBBAA" and "0xRRGGBB" are all accepted so a config edited
    // by hand does not have to guess the one true spelling.
    static bool parseHexColor(const std::string& in, Color4& out) {
        size_t i = 0;
        if (in.size() > 1 && in[0] == '#') i = 1;
        else if (in.size() > 2 && in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) i = 2;

        std::string digits = in.substr(i);
        if (digits.size() != 6 && digits.size() != 8) return false;
        for (char ch : digits) if (!isxdigit((unsigned char)ch)) return false;

        uint32_t rgb = (uint32_t)strtoul(digits.substr(0, 6).c_str(), nullptr, 16);
        float alpha = 1.0f;
        if (digits.size() == 8)
            alpha = (float)strtoul(digits.substr(6, 2).c_str(), nullptr, 16) / 255.0f;
        out = Color4::Hex(rgb, alpha);
        return true;
    }

    void overrideColor(const char* role, Color4& slot) const {
        std::string key = std::string("theme.overrides.color.") + role;
        std::string val = SilverConfig::get().text(key.c_str(), "");
        Color4 parsed;
        if (!val.empty() && parseHexColor(val, parsed)) slot = parsed;
    }

    void overrideSpring(const char* name, ThemeMotionSpec& spec) const {
        const SilverConfig& c = SilverConfig::get();
        std::string base = std::string("theme.overrides.motion.") + name + ".";
        spec.frequency = c.num((base + "frequency").c_str(), spec.frequency);
        spec.damping   = c.num((base + "damping").c_str(),   spec.damping);
        spec.response  = c.num((base + "response").c_str(),  spec.response);
    }

    // Every token can be overridden from config. Absent keys keep the theme's
    // own value, so an override file only has to name what it changes.
    void applyOverrides(ThemeTokens& t) const {
        ThemeColors& c = t.color;
        overrideColor("primary", c.primary);
        overrideColor("onPrimary", c.onPrimary);
        overrideColor("primaryContainer", c.primaryContainer);
        overrideColor("onPrimaryContainer", c.onPrimaryContainer);
        overrideColor("secondary", c.secondary);
        overrideColor("onSecondary", c.onSecondary);
        overrideColor("secondaryContainer", c.secondaryContainer);
        overrideColor("onSecondaryContainer", c.onSecondaryContainer);
        overrideColor("tertiary", c.tertiary);
        overrideColor("onTertiary", c.onTertiary);
        overrideColor("tertiaryContainer", c.tertiaryContainer);
        overrideColor("onTertiaryContainer", c.onTertiaryContainer);
        overrideColor("error", c.error);
        overrideColor("onError", c.onError);
        overrideColor("errorContainer", c.errorContainer);
        overrideColor("onErrorContainer", c.onErrorContainer);
        overrideColor("surface", c.surface);
        overrideColor("onSurface", c.onSurface);
        overrideColor("onSurfaceVariant", c.onSurfaceVariant);
        overrideColor("surfaceDim", c.surfaceDim);
        overrideColor("surfaceBright", c.surfaceBright);
        overrideColor("surfaceContainerLowest", c.surfaceContainerLowest);
        overrideColor("surfaceContainerLow", c.surfaceContainerLow);
        overrideColor("surfaceContainer", c.surfaceContainer);
        overrideColor("surfaceContainerHigh", c.surfaceContainerHigh);
        overrideColor("surfaceContainerHighest", c.surfaceContainerHighest);
        overrideColor("inverseSurface", c.inverseSurface);
        overrideColor("inverseOnSurface", c.inverseOnSurface);
        overrideColor("inversePrimary", c.inversePrimary);
        overrideColor("outline", c.outline);
        overrideColor("outlineVariant", c.outlineVariant);
        overrideColor("scrim", c.scrim);
        overrideColor("shadow", c.shadow);

        const SilverConfig& cfg = SilverConfig::get();
        ThemeShape& s = t.shape;
        s.extraSmall          = cfg.num("theme.overrides.shape.extraSmall", s.extraSmall);
        s.small               = cfg.num("theme.overrides.shape.small", s.small);
        s.medium              = cfg.num("theme.overrides.shape.medium", s.medium);
        s.large               = cfg.num("theme.overrides.shape.large", s.large);
        s.largeIncreased      = cfg.num("theme.overrides.shape.largeIncreased", s.largeIncreased);
        s.extraLarge          = cfg.num("theme.overrides.shape.extraLarge", s.extraLarge);
        s.extraLargeIncreased = cfg.num("theme.overrides.shape.extraLargeIncreased", s.extraLargeIncreased);
        s.extraExtraLarge     = cfg.num("theme.overrides.shape.extraExtraLarge", s.extraExtraLarge);
        s.pressMorph          = cfg.num("theme.overrides.shape.pressMorph", s.pressMorph);

        ThemeStateLayer& st = t.state;
        st.hover = cfg.num("theme.overrides.state.hover", st.hover);
        st.focus = cfg.num("theme.overrides.state.focus", st.focus);
        st.press = cfg.num("theme.overrides.state.press", st.press);
        st.drag  = cfg.num("theme.overrides.state.drag",  st.drag);

        overrideSpring("spatialFast",    t.motion.spatialFast);
        overrideSpring("spatialDefault", t.motion.spatialDefault);
        overrideSpring("spatialSlow",    t.motion.spatialSlow);
        overrideSpring("effectsFast",    t.motion.effectsFast);
        overrideSpring("effectsDefault", t.motion.effectsDefault);
        overrideSpring("effectsSlow",    t.motion.effectsSlow);
    }
};
