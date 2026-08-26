#pragma once

// Application glue for the Material component library.
//
// The library itself lives in include/material/ and knows nothing about this
// app. This header pulls its types into the global namespace (roughly 300 call
// sites predate the library and spell them unqualified) and keeps ThemePalette,
// the flat colour list the existing UI reads.
//
// ThemePalette is deliberately *not* part of the library: it is this app's
// legacy vocabulary, derived from the library's tokens so that both stay in
// step. New code should prefer the tokens and the components directly.

#include "material/material.h"

using material::Color4;
using material::ThemeColors;
using material::ThemeShape;
using material::ThemeStateLayer;
using material::ThemeTypeStyle;
using material::ThemeTypeScale;
using material::ThemeTypography;
using material::ThemeMotionSpec;
using material::ThemeMotion;
using material::ThemeElevationLevel;
using material::ThemeElevation;
using material::ThemeTokens;
using material::ThemeDefinition;
using material::ThemeRegistry;

// -----------------------------------------------------------------------------
// Legacy flat palette, derived from tokens
// -----------------------------------------------------------------------------

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

    // Surfaces that popups, menus and the settings panel used to hardcode as
    // literal hex pairs. Derived here so they follow the active theme.
    Color4 panelBg;        // settings panel / large floating surface
    Color4 menuBg;         // dropdown and popup body
    Color4 menuBorder;     // its hairline
    Color4 divider;        // separator rules inside a surface
    Color4 rowHover;       // hovered list row or chip
    Color4 chipIdle;       // resting chip / segmented control
    Color4 wellBg;         // recessed inset, darker than its container
    Color4 outlineStrong;  // emphasised outline, e.g. a drag rail
    Color4 accentSoft;     // tinted highlight behind a selected row
    Color4 starBadge;      // "starred" marker
    Color4 attention;      // needs-action badge, e.g. unsaved edits
};

// The alpha values here preserve the translucency the UI was designed with:
// bars and cards sit slightly see-through over the canvas.
inline ThemePalette paletteFromTokens(const ThemeTokens& t) {
    const ThemeColors& c = t.color;
    ThemePalette p;
    p.isDark = t.isDark;

    p.bgCanvas     = c.surface;
    p.barBg        = c.surfaceContainer.withAlpha(0.95f);
    p.barBorder    = c.outlineVariant.withAlpha(0.90f);
    p.cardBg       = c.surfaceContainerLow.withAlpha(0.98f);
    p.cardBorder   = c.outlineVariant;
    p.cardHeaderBg = c.surfaceContainerHigh;

    p.btnBg     = c.surfaceContainerHighest.withAlpha(0.85f);
    p.btnHover  = Color4::overlay(c.surfaceContainerHighest, c.onSurface, t.state.hover)
                      .withAlpha(0.95f);
    p.btnActive = c.primary;
    p.btnBorder = c.outline.withAlpha(0.80f);

    p.accent      = c.primary;
    // Hovering the accent lightens it toward its own on-colour in dark themes and
    // darkens it toward the surface in light ones, so it stays distinguishable.
    p.accentHover = t.isDark ? Color4::mix(c.primary, c.onPrimaryContainer, 0.28f)
                             : Color4::mix(c.primary, c.onPrimary, 0.18f);

    p.textPrimary   = c.onSurface;
    p.textSecondary = c.onSurfaceVariant;
    p.textMuted     = c.outline;
    p.textAccent    = c.primary;

    p.thumbBg           = c.surfaceContainerHigh;
    p.thumbActiveBorder = c.primary;

    p.toastBg     = c.surfaceContainer.withAlpha(0.96f);
    p.toastBorder = c.primary.withAlpha(0.90f);

    p.minimapBg           = c.surfaceContainerLowest.withAlpha(0.90f);
    p.minimapBorder       = c.outline.withAlpha(0.95f);
    p.minimapViewportBox  = c.primary.withAlpha(0.95f);

    p.panelBg       = c.surfaceContainerLow;
    p.menuBg        = c.surfaceContainerHigh;
    p.menuBorder    = c.outlineVariant;
    p.divider       = c.outlineVariant;
    p.rowHover      = c.surfaceContainerHighest;
    p.chipIdle      = c.surfaceContainer;
    p.wellBg        = c.surfaceContainerLowest;
    p.outlineStrong = c.outline;
    p.accentSoft    = c.primaryContainer;
    p.starBadge     = c.tertiary;
    p.attention     = c.tertiary;
    return p;
}
