#pragma once

#include <stdint.h>
#include <string>
#include <stdio.h>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <sys/stat.h>
#include <algorithm>

struct Color4 {
    float r, g, b, a;
    Color4() : r(1), g(1), b(1), a(1) {}
    Color4(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
    static Color4 Hex(uint32_t hex, float alpha = 1.0f) {
        float r = ((hex >> 16) & 0xFF) / 255.0f;
        float g = ((hex >> 8) & 0xFF) / 255.0f;
        float b = (hex & 0xFF) / 255.0f;
        return Color4(r, g, b, alpha);
    }
};

struct ThemePalette {
    bool isDark;
    Color4 bgCanvas;
    Color4 barBg;
    Color4 barBorder;
    Color4 cardBg;
    Color4 cardBorder;
    Color4 cardHeaderBg;
    Color4 btnBg;
    Color4 btnHover;
    Color4 btnActive;
    Color4 btnBorder;
    Color4 accent;
    Color4 accentHover;
    Color4 textPrimary;
    Color4 textSecondary;
    Color4 textMuted;
    Color4 textAccent;
    Color4 thumbBg;
    Color4 thumbActiveBorder;
    Color4 toastBg;
    Color4 toastBorder;
    Color4 minimapBg;
    Color4 minimapBorder;
    Color4 minimapViewportBox;
};

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
    ThemePalette current;

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

                    // 1. Check GNOME color-scheme ('prefer-dark' vs 'prefer-light' / 'default')
                    size_t pos = s.find("color-scheme");
                    if (pos != std::string::npos) {
                        std::string sub = s.substr(pos, std::min((size_t)100, s.size() - pos));
                        if (sub.find("prefer-dark") != std::string::npos) return true;
                        if (sub.find("prefer-light") != std::string::npos || sub.find("default") != std::string::npos) return false;
                    }

                    // 2. Check GTK theme ('dark' in theme name)
                    size_t gtkPos = s.find("gtk-theme");
                    if (gtkPos != std::string::npos) {
                        std::string gtkSub = s.substr(gtkPos, std::min((size_t)100, s.size() - gtkPos));
                        if (gtkSub.find("dark") != std::string::npos || gtkSub.find("Dark") != std::string::npos) return true;
                        if (gtkSub.find("light") != std::string::npos || gtkSub.find("Light") != std::string::npos) return false;
                    }
                }
            }
        }

        // 3. Fallback to Environment Variables
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
        if (m == THEME_DARK) {
            isDarkMode = true;
        } else if (m == THEME_LIGHT) {
            isDarkMode = false;
        } else {
            isDarkMode = detectSystemDark();
        }
        updatePalette();
    }

    void cycleThemeMode() {
        ThemeMode cur = mode.load();
        if (cur == THEME_SYSTEM) setThemeMode(THEME_DARK);
        else if (cur == THEME_DARK) setThemeMode(THEME_LIGHT);
        else setThemeMode(THEME_SYSTEM);
    }

    void toggleManual() {
        cycleThemeMode();
    }

    std::string getThemeModeName() const {
        ThemeMode m = mode.load();
        switch (m) {
            case THEME_DARK: return "Dark";
            case THEME_LIGHT: return "Light";
            case THEME_SYSTEM: return isDarkMode.load() ? "System (Dark)" : "System (Light)";
        }
        return "System";
    }

    void init() {
        setThemeMode(THEME_SYSTEM);

        const char* home = getenv("HOME");
        if (home) {
            std::string dconfPath = std::string(home) + "/.config/dconf/user";
            struct stat st;
            if (stat(dconfPath.c_str(), &st) == 0) {
                lastDconfMtime = st.st_mtime;
            }
        }
    }

    void update(float dt) {
        if (mode.load() != THEME_SYSTEM) return;

        pollTimer += dt;
        if (pollTimer >= 0.20f) { // Check 5 times a second instantly via stat (0.000005s)
            pollTimer = 0.0f;
            const char* home = getenv("HOME");
            if (home) {
                std::string dconfPath = std::string(home) + "/.config/dconf/user";
                struct stat st;
                if (stat(dconfPath.c_str(), &st) == 0) {
                    if (st.st_mtime != lastDconfMtime) {
                        lastDconfMtime = st.st_mtime;
                        bool sysDark = detectSystemDark();
                        if (sysDark != isDarkMode.load()) {
                            isDarkMode = sysDark;
                            updatePalette();
                        }
                    }
                }
            }
        }
    }

    void updatePalette() {
        bool dark = isDarkMode.load();
        if (dark) {
            current.isDark = true;
            current.bgCanvas = Color4::Hex(0x111215, 1.0f);
            current.barBg = Color4::Hex(0x18191E, 0.95f);
            current.barBorder = Color4::Hex(0x282933, 0.90f);
            current.cardBg = Color4::Hex(0x191A20, 0.98f);
            current.cardBorder = Color4::Hex(0x2E303C, 1.0f);
            current.cardHeaderBg = Color4::Hex(0x142830, 1.0f);
            current.btnBg = Color4::Hex(0x22232B, 0.85f);
            current.btnHover = Color4::Hex(0x30323E, 0.95f);
            current.btnActive = Color4::Hex(0x0284C7, 1.0f);
            current.btnBorder = Color4::Hex(0x343644, 0.80f);
            current.accent = Color4::Hex(0x0284C7, 1.0f);
            current.accentHover = Color4::Hex(0x38BDF8, 1.0f);
            current.textPrimary = Color4::Hex(0xF1F5F9, 1.0f);
            current.textSecondary = Color4::Hex(0x94A3B8, 1.0f);
            current.textMuted = Color4::Hex(0x64748B, 1.0f);
            current.textAccent = Color4::Hex(0x38BDF8, 1.0f);
            current.thumbBg = Color4::Hex(0x1E1F27, 1.0f);
            current.thumbActiveBorder = Color4::Hex(0x0284C7, 1.0f);
            current.toastBg = Color4::Hex(0x18191E, 0.96f);
            current.toastBorder = Color4::Hex(0x0284C7, 0.90f);
            current.minimapBg = Color4::Hex(0x121317, 0.90f);
            current.minimapBorder = Color4::Hex(0x3E4150, 0.95f);
            current.minimapViewportBox = Color4(1.0f, 1.0f, 1.0f, 0.95f);
        } else {
            current.isDark = false;
            current.bgCanvas = Color4::Hex(0xF1F3F6, 1.0f);
            current.barBg = Color4::Hex(0xFFFFFF, 0.95f);
            current.barBorder = Color4::Hex(0xE2E8F0, 0.90f);
            current.cardBg = Color4::Hex(0xFFFFFF, 0.98f);
            current.cardBorder = Color4::Hex(0xD1D5DB, 1.0f);
            current.cardHeaderBg = Color4::Hex(0xDBEAFE, 1.0f);
            current.btnBg = Color4::Hex(0xF1F5F9, 0.90f);
            current.btnHover = Color4::Hex(0xE2E8F0, 1.0f);
            current.btnActive = Color4::Hex(0x0284C7, 1.0f);
            current.btnBorder = Color4::Hex(0xCBD5E1, 0.80f);
            current.accent = Color4::Hex(0x0284C7, 1.0f);
            current.accentHover = Color4::Hex(0x0369A1, 1.0f);
            current.textPrimary = Color4::Hex(0x0F172A, 1.0f);
            current.textSecondary = Color4::Hex(0x475569, 1.0f);
            current.textMuted = Color4::Hex(0x94A3B8, 1.0f);
            current.textAccent = Color4::Hex(0x0284C7, 1.0f);
            current.thumbBg = Color4::Hex(0xE2E8F0, 1.0f);
            current.thumbActiveBorder = Color4::Hex(0x0284C7, 1.0f);
            current.toastBg = Color4::Hex(0xFFFFFF, 0.96f);
            current.toastBorder = Color4::Hex(0x0284C7, 0.90f);
            current.minimapBg = Color4::Hex(0xFFFFFF, 0.90f);
            current.minimapBorder = Color4::Hex(0x94A3B8, 0.95f);
            current.minimapViewportBox = Color4(0.0f, 0.4f, 0.9f, 0.95f);
        }
    }
};
