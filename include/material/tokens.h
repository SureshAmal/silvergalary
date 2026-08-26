#pragma once

// Design tokens: colour, shape, state, typography, motion, elevation.
//
// Part of the Material component library. Depends on nothing outside this
// directory: no application types, no config, no renderer.


#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace material {

struct Color4 {
    float r, g, b, a;
    Color4() : r(1), g(1), b(1), a(1) {}
    Color4(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}

    static Color4 Hex(uint32_t hex, float alpha = 1.0f) {
        return Color4(((hex >> 16) & 0xFF) / 255.0f,
                      ((hex >> 8) & 0xFF) / 255.0f,
                      (hex & 0xFF) / 255.0f,
                      alpha);
    }

    Color4 withAlpha(float alpha) const { return Color4(r, g, b, alpha); }

    // Source-over composite of `over` onto this colour. Material's state layers
    // are exactly this: the on-colour laid over the container at a fixed opacity.
    static Color4 overlay(const Color4& base, const Color4& over, float opacity) {
        float t = std::clamp(opacity, 0.0f, 1.0f) * over.a;
        return Color4(base.r + (over.r - base.r) * t,
                      base.g + (over.g - base.g) * t,
                      base.b + (over.b - base.b) * t,
                      base.a);
    }

    static Color4 mix(const Color4& a0, const Color4& b0, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color4(a0.r + (b0.r - a0.r) * t,
                      a0.g + (b0.g - a0.g) * t,
                      a0.b + (b0.b - a0.b) * t,
                      a0.a + (b0.a - a0.a) * t);
    }

    // Relative luminance (WCAG). Used to pick a readable on-colour when a theme
    // leaves one unspecified.
    float luminance() const {
        auto lin = [](float c) {
            return (c <= 0.03928f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126f * lin(r) + 0.7152f * lin(g) + 0.0722f * lin(b);
    }
};

// -----------------------------------------------------------------------------
// Token groups
// -----------------------------------------------------------------------------

struct ThemeColors {
    Color4 primary, onPrimary, primaryContainer, onPrimaryContainer;
    Color4 secondary, onSecondary, secondaryContainer, onSecondaryContainer;
    Color4 tertiary, onTertiary, tertiaryContainer, onTertiaryContainer;
    Color4 error, onError, errorContainer, onErrorContainer;

    Color4 surface, onSurface, onSurfaceVariant;
    Color4 surfaceDim, surfaceBright;
    Color4 surfaceContainerLowest, surfaceContainerLow, surfaceContainer;
    Color4 surfaceContainerHigh, surfaceContainerHighest;

    Color4 inverseSurface, inverseOnSurface, inversePrimary;
    Color4 outline, outlineVariant;
    Color4 scrim, shadow;
};

// Corner radii in points. Material 3 Expressive extends the baseline scale with
// three intermediate tiers - largeIncreased, extraLargeIncreased and
// extraExtraLarge - because Expressive layouts lean on rounder, more varied
// containers than baseline M3 does. "full" means stadium; callers clamp it to
// half the shorter side.
struct ThemeShape {
    float none                = 0.0f;
    float extraSmall          = 4.0f;
    float small               = 8.0f;
    float medium              = 12.0f;
    float large               = 16.0f;
    float largeIncreased      = 20.0f;
    float extraLarge          = 28.0f;
    float extraLargeIncreased = 32.0f;
    float extraExtraLarge     = 48.0f;
    float full                = 9999.0f;

    // Expressive's signature: a container does not just tint when pressed, it
    // changes shape. Pressed corners travel toward pressMorph x the rest radius.
    // 1.0 disables morphing, which is what the non-Expressive themes use.
    float pressMorph = 0.60f;

    // Radius for a container at rest radius `r`, `pressed` in 0..1.
    float morph(float r, float pressed) const {
        return r + (r * pressMorph - r) * std::clamp(pressed, 0.0f, 1.0f);
    }
};

// Opacity of the on-colour laid over a container to signal interaction state.
struct ThemeStateLayer {
    float hover = 0.08f;
    float focus = 0.10f;
    float press = 0.10f;
    float drag  = 0.16f;
};

struct ThemeTypeStyle {
    float size = 14.0f;        // points, before the app's UI scale
    float lineHeight = 20.0f;
    float tracking = 0.0f;     // letter spacing, points
    int   weight = 400;
};

struct ThemeTypeScale {
    ThemeTypeStyle displayLarge, displayMedium, displaySmall;
    ThemeTypeStyle headlineLarge, headlineMedium, headlineSmall;
    ThemeTypeStyle titleLarge, titleMedium, titleSmall;
    ThemeTypeStyle bodyLarge, bodyMedium, bodySmall;
    ThemeTypeStyle labelLarge, labelMedium, labelSmall;
};

// Expressive pairs every role with an *emphasized* cut - same metrics, heavier
// weight and tighter tracking - so hierarchy can be expressed by weight instead
// of by size alone. Baseline M3 has no such pair.
struct ThemeTypography {
    ThemeTypeScale standard;
    ThemeTypeScale emphasized;
};

// Feeds SpringParams::set(frequency, damping, response) in silver_anim.h, so a
// theme controls how the whole UI moves without touching any animation code.
struct ThemeMotionSpec {
    float frequency = 3.0f;   // Hz
    float damping   = 0.8f;   // zeta: < 1 overshoots, 1 is critical
    float response  = 0.0f;   // > 0 leads the target, < 0 anticipates

    // Material publishes springs as (stiffness, damping ratio) for unit mass.
    static ThemeMotionSpec fromStiffness(float stiffness, float damping, float response = 0.0f) {
        ThemeMotionSpec s;
        s.frequency = std::sqrt(std::max(0.0001f, stiffness)) / 6.2831853f;
        s.damping = damping;
        s.response = response;
        return s;
    }
};

struct ThemeMotion {
    // Spatial springs move things through space and may overshoot.
    ThemeMotionSpec spatialFast, spatialDefault, spatialSlow;
    // Effect springs drive colour and opacity, and must never overshoot.
    ThemeMotionSpec effectsFast, effectsDefault, effectsSlow;
};

// This renderer draws shadows as an offset quad, not a gaussian, so an elevation
// level is an offset plus an alpha rather than a blur radius.
struct ThemeElevationLevel {
    float offsetY = 0.0f;
    float spread  = 0.0f;
    float alpha   = 0.0f;
};

struct ThemeElevation {
    ThemeElevationLevel level[6];
};

struct ThemeTokens {
    bool isDark = true;
    ThemeColors     color;
    ThemeShape      shape;
    ThemeStateLayer state;
    ThemeTypography type;
    ThemeMotion     motion;
    ThemeElevation  elevation;
};

} // namespace material
