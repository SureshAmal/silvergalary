#pragma once

// Containers: cards, menus, dialogs, app bars.
//
// Material distinguishes these mostly by which surfaceContainer tier they sit
// on and how much elevation they carry, so they share one implementation.

#include "state.h"

namespace material {

enum class SurfaceKind { Card, Menu, Dialog, AppBar, Sheet };

struct SurfaceStyle {
    Color4 background;
    float radius = 0.0f;
    int elevation = 0;
    bool outlined = false;
};

inline SurfaceStyle surfaceStyle(SurfaceKind kind, const ThemeTokens& t) {
    const ThemeColors& c = t.color;
    SurfaceStyle s;
    switch (kind) {
        case SurfaceKind::Card:
            s.background = c.surfaceContainerLow; s.radius = t.shape.medium;
            s.elevation = 1; break;
        case SurfaceKind::Menu:
            s.background = c.surfaceContainerHigh; s.radius = t.shape.extraSmall;
            s.elevation = 2; break;
        case SurfaceKind::Dialog:
            s.background = c.surfaceContainerHigh; s.radius = t.shape.extraLarge;
            s.elevation = 3; break;
        case SurfaceKind::AppBar:
            // A top app bar is flush with the window: no radius, and elevation
            // only once content scrolls beneath it.
            s.background = c.surfaceContainer; s.radius = 0.0f; s.elevation = 0; break;
        case SurfaceKind::Sheet:
            s.background = c.surfaceContainerLow; s.radius = t.shape.extraLarge;
            s.elevation = 1; break;
    }
    return s;
}

template <class Painter>
inline void drawSurface(Painter& p, const Rect& r, const SurfaceStyle& s,
                        const ThemeTokens& t, float alpha = 1.0f) {
    if (s.elevation > 0) drawElevation(p, r, s.radius, t, s.elevation, alpha);
    p.addRoundedRect(r.x, r.y, r.w, r.h, s.radius,
                     s.background.withAlpha(s.background.a * alpha));
    if (s.outlined) {
        Color4 line = t.color.outlineVariant;
        p.addRoundedBorder(r.x, r.y, r.w, r.h, s.radius, 1.0f,
                           line.withAlpha(line.a * alpha));
    }
}

template <class Painter>
inline void drawSurface(Painter& p, const Rect& r, SurfaceKind kind,
                        const ThemeTokens& t, float alpha = 1.0f, bool outlined = true) {
    SurfaceStyle s = surfaceStyle(kind, t);
    s.outlined = outlined;
    drawSurface(p, r, s, t, alpha);
}

// An app bar gains a hairline, then elevation, once content scrolls under it -
// this is how Material expresses that the bar is now above the content.
template <class Painter>
inline void drawAppBar(Painter& p, const Rect& r, const ThemeTokens& t,
                       bool scrolledUnder, float alpha = 1.0f) {
    const ThemeColors& c = t.color;
    Color4 bg = scrolledUnder ? c.surfaceContainer : c.surface;
    p.addRect(r.x, r.y, r.w, r.h, bg.withAlpha(bg.a * alpha));
    if (scrolledUnder) {
        Color4 line = c.outlineVariant;
        p.addRect(r.x, r.y + r.h - 1.0f, r.w, 1.0f, line.withAlpha(line.a * alpha));
    }
}

// A modal scrim dims everything behind a dialog or sheet.
template <class Painter>
inline void drawScrim(Painter& p, const Rect& r, const ThemeTokens& t, float amount) {
    if (amount <= 0.0f) return;
    p.addRect(r.x, r.y, r.w, r.h, t.color.scrim.withAlpha(0.32f * amount));
}

} // namespace material
