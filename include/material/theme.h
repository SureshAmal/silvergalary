#pragma once

// Theme definitions and the registry that themes are swapped through.
//
// Part of the Material component library. Depends on nothing outside this
// directory: no application types, no config, no renderer.


#include "tokens.h"

namespace material {

// -----------------------------------------------------------------------------
// Theme definitions and registry
// -----------------------------------------------------------------------------

struct ThemeDefinition {
    std::string id;      // stable key stored in config, e.g. "material-expressive"
    std::string label;   // shown in the settings palette
    ThemeTokens dark;
    ThemeTokens light;

    const ThemeTokens& variant(bool wantDark) const { return wantDark ? dark : light; }
};

namespace builtin {

inline ThemeTypeStyle typeStyle(float size, float line, float track, int weight) {
    ThemeTypeStyle s;
    s.size = size; s.lineHeight = line; s.tracking = track; s.weight = weight;
    return s;
}

inline ThemeTypography expressiveTypography() {
    ThemeTypography t;
    ThemeTypeScale& n = t.standard;
    n.displayLarge   = typeStyle(57, 64, -0.25f, 400);
    n.displayMedium  = typeStyle(45, 52,  0.0f,  400);
    n.displaySmall   = typeStyle(36, 44,  0.0f,  400);
    n.headlineLarge  = typeStyle(32, 40,  0.0f,  400);
    n.headlineMedium = typeStyle(28, 36,  0.0f,  400);
    n.headlineSmall  = typeStyle(24, 32,  0.0f,  400);
    n.titleLarge     = typeStyle(22, 28,  0.0f,  400);
    n.titleMedium    = typeStyle(16, 24,  0.15f, 500);
    n.titleSmall     = typeStyle(14, 20,  0.10f, 500);
    n.bodyLarge      = typeStyle(16, 24,  0.50f, 400);
    n.bodyMedium     = typeStyle(14, 20,  0.25f, 400);
    n.bodySmall      = typeStyle(12, 16,  0.40f, 400);
    n.labelLarge     = typeStyle(14, 20,  0.10f, 500);
    n.labelMedium    = typeStyle(12, 16,  0.50f, 500);
    n.labelSmall     = typeStyle(11, 16,  0.50f, 500);

    // Emphasized keeps the metrics and moves weight up a step (and tracking in,
    // since heavier faces need less letter spacing at the same size).
    ThemeTypeScale& e = t.emphasized;
    e = n;
    ThemeTypeStyle* big[] = { &e.displayLarge, &e.displayMedium, &e.displaySmall,
                              &e.headlineLarge, &e.headlineMedium, &e.headlineSmall,
                              &e.titleLarge };
    for (ThemeTypeStyle* st : big) { st->weight = 500; st->tracking -= 0.10f; }
    ThemeTypeStyle* mid[] = { &e.titleMedium, &e.titleSmall,
                              &e.bodyLarge, &e.bodyMedium, &e.bodySmall };
    for (ThemeTypeStyle* st : mid) { st->weight = 600; st->tracking -= 0.05f; }
    ThemeTypeStyle* lbl[] = { &e.labelLarge, &e.labelMedium, &e.labelSmall };
    for (ThemeTypeStyle* st : lbl) { st->weight = 700; }
    return t;
}

// Material 3 Expressive leans on lower damping and higher stiffness than the
// classic set: motion arrives quickly and settles with a small, deliberate
// overshoot. Effect springs stay critically damped - colour must not bounce.
inline ThemeMotion expressiveMotion() {
    ThemeMotion m;
    m.spatialFast    = ThemeMotionSpec::fromStiffness(800.0f, 0.60f, 0.15f);
    m.spatialDefault = ThemeMotionSpec::fromStiffness(380.0f, 0.75f, 0.15f);
    m.spatialSlow    = ThemeMotionSpec::fromStiffness(200.0f, 0.80f, 0.10f);
    m.effectsFast    = ThemeMotionSpec::fromStiffness(3800.0f, 1.0f, 0.0f);
    m.effectsDefault = ThemeMotionSpec::fromStiffness(1600.0f, 1.0f, 0.0f);
    m.effectsSlow    = ThemeMotionSpec::fromStiffness(800.0f,  1.0f, 0.0f);
    return m;
}

inline ThemeMotion standardMotion() {
    ThemeMotion m;
    m.spatialFast    = ThemeMotionSpec::fromStiffness(1400.0f, 0.90f, 0.0f);
    m.spatialDefault = ThemeMotionSpec::fromStiffness(700.0f,  0.90f, 0.0f);
    m.spatialSlow    = ThemeMotionSpec::fromStiffness(300.0f,  0.95f, 0.0f);
    m.effectsFast    = ThemeMotionSpec::fromStiffness(3800.0f, 1.0f, 0.0f);
    m.effectsDefault = ThemeMotionSpec::fromStiffness(1600.0f, 1.0f, 0.0f);
    m.effectsSlow    = ThemeMotionSpec::fromStiffness(800.0f,  1.0f, 0.0f);
    return m;
}

inline ThemeElevation materialElevation() {
    ThemeElevation e;
    const float offsets[6] = { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f };
    const float spreads[6] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f };
    const float alphas[6]  = { 0.0f, 0.16f, 0.20f, 0.24f, 0.28f, 0.32f };
    for (int i = 0; i < 6; ++i) {
        e.level[i].offsetY = offsets[i];
        e.level[i].spread  = spreads[i];
        e.level[i].alpha   = alphas[i];
    }
    return e;
}

// Expressive rounds everything a tier harder than baseline M3 and turns on
// press morphing.
inline ThemeShape expressiveShape() {
    ThemeShape s;
    s.extraSmall          = 8.0f;
    s.small               = 12.0f;
    s.medium              = 16.0f;
    s.large               = 24.0f;
    s.largeIncreased      = 28.0f;
    s.extraLarge          = 36.0f;
    s.extraLargeIncreased = 44.0f;
    s.extraExtraLarge     = 56.0f;
    s.pressMorph          = 0.55f;
    return s;
}

inline ThemeDefinition materialExpressive() {
    ThemeDefinition d;
    d.id = "material-expressive";
    d.label = "Material Expressive";

    // Expressive pushes chroma past the baseline scheme on the *accent* ramps -
    // a vivid violet primary and a hot-pink tertiary. Neutrals deliberately stay
    // close to grey with only a hint of the seed hue: raising their chroma too
    // is what turns every surface in a light theme pink.
    ThemeColors dark;
    dark.primary = Color4::Hex(0xE0B6FF);
    dark.onPrimary = Color4::Hex(0x46008E);
    dark.primaryContainer = Color4::Hex(0x6300B4);
    dark.onPrimaryContainer = Color4::Hex(0xF3DAFF);
    dark.secondary = Color4::Hex(0xCFC3DC);
    dark.onSecondary = Color4::Hex(0x3B2947);
    dark.secondaryContainer = Color4::Hex(0x4A4458);
    dark.onSecondaryContainer = Color4::Hex(0xFFD8FF);
    dark.tertiary = Color4::Hex(0xFFB1C8);
    dark.onTertiary = Color4::Hex(0x65003C);
    dark.tertiaryContainer = Color4::Hex(0x8E0055);
    dark.onTertiaryContainer = Color4::Hex(0xFFD9E4);
    dark.error = Color4::Hex(0xFFB4AB);
    dark.onError = Color4::Hex(0x690005);
    dark.errorContainer = Color4::Hex(0x93000A);
    dark.onErrorContainer = Color4::Hex(0xFFDAD6);
    dark.surface = Color4::Hex(0x141316);
    dark.onSurface = Color4::Hex(0xE9E0EB);
    dark.onSurfaceVariant = Color4::Hex(0xD0C3D6);
    dark.surfaceDim = Color4::Hex(0x16121A);
    dark.surfaceBright = Color4::Hex(0x3D3742);
    dark.surfaceContainerLowest = Color4::Hex(0x0F0E11);
    dark.surfaceContainerLow = Color4::Hex(0x1C1B1F);
    dark.surfaceContainer = Color4::Hex(0x201F23);
    dark.surfaceContainerHigh = Color4::Hex(0x2B292E);
    dark.surfaceContainerHighest = Color4::Hex(0x363439);
    dark.inverseSurface = Color4::Hex(0xE9E0EB);
    dark.inverseOnSurface = Color4::Hex(0x342F38);
    dark.inversePrimary = Color4::Hex(0x7E29C0);
    dark.outline = Color4::Hex(0x9A8DA1);
    dark.outlineVariant = Color4::Hex(0x4D4553);
    dark.scrim = Color4::Hex(0x000000);
    dark.shadow = Color4::Hex(0x000000);

    ThemeColors light;
    light.primary = Color4::Hex(0x7E29C0);
    light.onPrimary = Color4::Hex(0xFFFFFF);
    light.primaryContainer = Color4::Hex(0xEADDFF);
    light.onPrimaryContainer = Color4::Hex(0x2E004E);
    light.secondary = Color4::Hex(0x635B70);
    light.onSecondary = Color4::Hex(0xFFFFFF);
    light.secondaryContainer = Color4::Hex(0xE9DEF8);
    light.onSecondaryContainer = Color4::Hex(0x251430);
    light.tertiary = Color4::Hex(0xB4006F);
    light.onTertiary = Color4::Hex(0xFFFFFF);
    light.tertiaryContainer = Color4::Hex(0xFFD9E4);
    light.onTertiaryContainer = Color4::Hex(0x3E0021);
    light.error = Color4::Hex(0xBA1A1A);
    light.onError = Color4::Hex(0xFFFFFF);
    light.errorContainer = Color4::Hex(0xFFDAD6);
    light.onErrorContainer = Color4::Hex(0x410002);
    light.surface = Color4::Hex(0xFCF8FC);
    light.onSurface = Color4::Hex(0x1C1B1E);
    light.onSurfaceVariant = Color4::Hex(0x4A454E);
    light.surfaceDim = Color4::Hex(0xDDD8DE);
    light.surfaceBright = Color4::Hex(0xFCF8FC);
    light.surfaceContainerLowest = Color4::Hex(0xFFFFFF);
    light.surfaceContainerLow = Color4::Hex(0xF7F3F7);
    light.surfaceContainer = Color4::Hex(0xF1EDF2);
    light.surfaceContainerHigh = Color4::Hex(0xEBE7EC);
    light.surfaceContainerHighest = Color4::Hex(0xE6E1E6);
    light.inverseSurface = Color4::Hex(0x342F38);
    light.inverseOnSurface = Color4::Hex(0xF8EEF9);
    light.inversePrimary = Color4::Hex(0xE0B6FF);
    light.outline = Color4::Hex(0x7B7480);
    light.outlineVariant = Color4::Hex(0xCBC4CD);
    light.scrim = Color4::Hex(0x000000);
    light.shadow = Color4::Hex(0x000000);

    d.dark.isDark = true;
    d.dark.color = dark;
    d.light.isDark = false;
    d.light.color = light;
    for (ThemeTokens* t : { &d.dark, &d.light }) {
        t->shape = expressiveShape();
        t->type = expressiveTypography();
        t->motion = expressiveMotion();
        t->elevation = materialElevation();
    }
    return d;
}

// The look the app shipped with, kept selectable so switching themes is
// reversible and so there is a second theme to prove the layer works.
inline ThemeDefinition classicSlate() {
    ThemeDefinition d;
    d.id = "classic-slate";
    d.label = "Classic Slate";

    ThemeColors dark;
    dark.primary = Color4::Hex(0x0284C7);
    dark.onPrimary = Color4::Hex(0xFFFFFF);
    dark.primaryContainer = Color4::Hex(0x142830);
    dark.onPrimaryContainer = Color4::Hex(0x38BDF8);
    dark.secondary = Color4::Hex(0x94A3B8);
    dark.onSecondary = Color4::Hex(0x0F172A);
    dark.secondaryContainer = Color4::Hex(0x22232B);
    dark.onSecondaryContainer = Color4::Hex(0xE2E8F0);
    dark.tertiary = Color4::Hex(0x38BDF8);
    dark.onTertiary = Color4::Hex(0x0F172A);
    dark.tertiaryContainer = Color4::Hex(0x1E3A4C);
    dark.onTertiaryContainer = Color4::Hex(0xBAE6FD);
    dark.error = Color4::Hex(0xF87171);
    dark.onError = Color4::Hex(0x450A0A);
    dark.errorContainer = Color4::Hex(0x7F1D1D);
    dark.onErrorContainer = Color4::Hex(0xFECACA);
    dark.surface = Color4::Hex(0x111215);
    dark.onSurface = Color4::Hex(0xF1F5F9);
    dark.onSurfaceVariant = Color4::Hex(0x94A3B8);
    dark.surfaceDim = Color4::Hex(0x111215);
    dark.surfaceBright = Color4::Hex(0x343644);
    dark.surfaceContainerLowest = Color4::Hex(0x121317);
    dark.surfaceContainerLow = Color4::Hex(0x191A20);
    dark.surfaceContainer = Color4::Hex(0x18191E);
    dark.surfaceContainerHigh = Color4::Hex(0x1E1F27);
    dark.surfaceContainerHighest = Color4::Hex(0x22232B);
    dark.inverseSurface = Color4::Hex(0xF1F5F9);
    dark.inverseOnSurface = Color4::Hex(0x18191E);
    dark.inversePrimary = Color4::Hex(0x0369A1);
    dark.outline = Color4::Hex(0x64748B);
    dark.outlineVariant = Color4::Hex(0x282933);
    dark.scrim = Color4::Hex(0x000000);
    dark.shadow = Color4::Hex(0x000000);

    ThemeColors light;
    light.primary = Color4::Hex(0x0284C7);
    light.onPrimary = Color4::Hex(0xFFFFFF);
    light.primaryContainer = Color4::Hex(0xDBEAFE);
    light.onPrimaryContainer = Color4::Hex(0x0C4A6E);
    light.secondary = Color4::Hex(0x475569);
    light.onSecondary = Color4::Hex(0xFFFFFF);
    light.secondaryContainer = Color4::Hex(0xE2E8F0);
    light.onSecondaryContainer = Color4::Hex(0x0F172A);
    light.tertiary = Color4::Hex(0x0369A1);
    light.onTertiary = Color4::Hex(0xFFFFFF);
    light.tertiaryContainer = Color4::Hex(0xBAE6FD);
    light.onTertiaryContainer = Color4::Hex(0x082F49);
    light.error = Color4::Hex(0xB91C1C);
    light.onError = Color4::Hex(0xFFFFFF);
    light.errorContainer = Color4::Hex(0xFEE2E2);
    light.onErrorContainer = Color4::Hex(0x450A0A);
    light.surface = Color4::Hex(0xF1F3F6);
    light.onSurface = Color4::Hex(0x0F172A);
    light.onSurfaceVariant = Color4::Hex(0x475569);
    light.surfaceDim = Color4::Hex(0xD8DDE5);
    light.surfaceBright = Color4::Hex(0xFFFFFF);
    light.surfaceContainerLowest = Color4::Hex(0xFFFFFF);
    light.surfaceContainerLow = Color4::Hex(0xFFFFFF);
    light.surfaceContainer = Color4::Hex(0xFFFFFF);
    light.surfaceContainerHigh = Color4::Hex(0xE2E8F0);
    light.surfaceContainerHighest = Color4::Hex(0xF1F5F9);
    light.inverseSurface = Color4::Hex(0x0F172A);
    light.inverseOnSurface = Color4::Hex(0xF1F5F9);
    light.inversePrimary = Color4::Hex(0x38BDF8);
    light.outline = Color4::Hex(0x94A3B8);
    light.outlineVariant = Color4::Hex(0xE2E8F0);
    light.scrim = Color4::Hex(0x000000);
    light.shadow = Color4::Hex(0x000000);

    d.dark.isDark = true;
    d.dark.color = dark;
    d.light.isDark = false;
    d.light.color = light;
    for (ThemeTokens* t : { &d.dark, &d.light }) {
        ThemeShape baseline;              // baseline M3 scale, no press morph
        baseline.pressMorph = 1.0f;
        t->shape = baseline;
        t->type = expressiveTypography();
        t->motion = standardMotion();
        t->elevation = materialElevation();
    }
    return d;
}

} // namespace builtin

// -----------------------------------------------------------------------------
// Registry
// -----------------------------------------------------------------------------

class ThemeRegistry {
public:
    static ThemeRegistry& get() {
        static ThemeRegistry instance;
        return instance;
    }

    // Replaces any theme already registered under the same id, so a later
    // registration (a user theme, say) can override a built-in.
    void add(const ThemeDefinition& def) {
        for (ThemeDefinition& existing : themes) {
            if (existing.id == def.id) { existing = def; return; }
        }
        themes.push_back(def);
    }

    const ThemeDefinition* find(const std::string& id) const {
        for (const ThemeDefinition& t : themes)
            if (t.id == id) return &t;
        return nullptr;
    }

    const ThemeDefinition& defaultTheme() const { return themes.front(); }

    const std::vector<ThemeDefinition>& all() const { return themes; }

    std::vector<std::string> ids() const {
        std::vector<std::string> out;
        out.reserve(themes.size());
        for (const ThemeDefinition& t : themes) out.push_back(t.id);
        return out;
    }

private:
    ThemeRegistry() {
        // First entry is the default when config names an unknown theme.
        add(builtin::materialExpressive());
        add(builtin::classicSlate());
    }
    std::vector<ThemeDefinition> themes;
};


} // namespace material
