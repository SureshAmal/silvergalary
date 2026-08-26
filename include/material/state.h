#pragma once

// State layers and container/content role pairs.
//
// The single rule the hand-rolled call sites kept breaking: a filled surface is
// always drawn together with its *on-* colour. Writing white on `primary` is
// legible only when primary happens to be dark, which is true in a light theme
// and false in a dark one. Pair makes the two travel together.

#include "painter.h"

namespace material {

struct Pair {
    Color4 container;
    Color4 content;

    Pair fade(float alpha) const {
        return Pair{ container.withAlpha(container.a * alpha),
                     content.withAlpha(content.a * alpha) };
    }
};

inline float stateOpacity(const ThemeStateLayer& s, const Interaction& in) {
    if (in.disabled) return 0.0f;
    if (in.pressed)  return s.press;
    if (in.focused)  return s.focus;
    if (in.hovered)  return s.hover;
    return 0.0f;
}

// Material signals interaction by compositing the on-colour over the container
// at a fixed opacity, not by swapping in a different colour. The surface and its
// content keep the same relationship in every state.
inline Color4 withState(const Color4& container, const Color4& onColor,
                        const ThemeStateLayer& s, const Interaction& in) {
    float o = stateOpacity(s, in);
    return (o > 0.0f) ? Color4::overlay(container, onColor, o) : container;
}

// ---------------------------------------------------------------------------
// Standard role pairs
// ---------------------------------------------------------------------------

inline Pair rolePrimary(const ThemeColors& c)   { return { c.primary,            c.onPrimary }; }
inline Pair roleTonal(const ThemeColors& c)     { return { c.secondaryContainer, c.onSecondaryContainer }; }
inline Pair rolePrimaryTonal(const ThemeColors& c) { return { c.primaryContainer, c.onPrimaryContainer }; }
inline Pair roleTertiary(const ThemeColors& c)  { return { c.tertiaryContainer,  c.onTertiaryContainer }; }
inline Pair roleError(const ThemeColors& c)     { return { c.errorContainer,     c.onErrorContainer }; }
inline Pair roleSurface(const ThemeColors& c)   { return { c.surfaceContainer,   c.onSurface }; }
inline Pair roleTransparent(const ThemeColors& c) { return { Color4(0,0,0,0),    c.onSurface }; }

// Selection uses secondaryContainer, never primary: primary is the strong fill
// reserved for one dominant action, and reusing it for selection is what forces
// the unreadable pairing in dark themes.
inline Pair roleSelected(const ThemeColors& c)  { return roleTonal(c); }

// ---------------------------------------------------------------------------
// Elevation
// ---------------------------------------------------------------------------

// This library draws shadows as an offset quad rather than a gaussian, so an
// elevation level resolves to an offset and an alpha.
template <class Painter>
inline void drawElevation(Painter& p, const Rect& r, float radius,
                          const ThemeTokens& t, int level, float alpha = 1.0f) {
    if (level <= 0 || level > 5) return;
    const ThemeElevationLevel& e = t.elevation.level[level];
    if (e.alpha <= 0.0f) return;
    p.addRoundedRect(r.x - e.spread * 0.5f, r.y + e.offsetY,
                     r.w + e.spread, r.h + e.spread, radius,
                     t.color.shadow.withAlpha(e.alpha * alpha));
}

} // namespace material
