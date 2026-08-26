#pragma once

// Buttons: filled, tonal, outlined, text, and icon.
//
// Material buttons are stadium-shaped by default; Expressive keeps that and
// morphs the corners inward while pressed. Each variant differs only in which
// role pair it uses and whether it carries an outline, so they share one body.

#include "state.h"

namespace material {

enum class ButtonVariant { Filled, Tonal, Outlined, Text, Elevated };

inline Pair buttonRole(ButtonVariant v, const ThemeColors& c) {
    switch (v) {
        case ButtonVariant::Filled:   return rolePrimary(c);
        case ButtonVariant::Tonal:    return roleTonal(c);
        case ButtonVariant::Elevated: return { c.surfaceContainerLow, c.primary };
        case ButtonVariant::Outlined: return { Color4(0,0,0,0), c.primary };
        case ButtonVariant::Text:     return { Color4(0,0,0,0), c.primary };
    }
    return rolePrimary(c);
}

// Draws the container and returns the colour the caller should use for the
// label or icon, so the two can never be chosen independently.
template <class Painter>
inline Color4 drawButton(Painter& p, const Rect& r, ButtonVariant v,
                         const ThemeTokens& t, const Interaction& in,
                         float alpha = 1.0f, int elevation = 0) {
    const ThemeColors& c = t.color;
    Pair role = buttonRole(v, c);

    float rest = t.shape.full;
    float radius = t.shape.morph(std::min(rest, std::min(r.w, r.h) * 0.5f),
                                 in.pressed ? 1.0f : 0.0f);

    if (elevation > 0 && !in.disabled) drawElevation(p, r, radius, t, elevation, alpha);

    Color4 fill = withState(role.container, role.content, t.state, in);
    fill = applyDisabled(fill, in.disabled, false);
    if (fill.a > 0.001f)
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));

    if (v == ButtonVariant::Outlined) {
        Color4 line = applyDisabled(c.outline, in.disabled, false);
        p.addRoundedBorder(r.x, r.y, r.w, r.h, radius,
                           std::max(1.0f, r.h * 0.045f), line.withAlpha(line.a * alpha));
    }

    Color4 content = applyDisabled(role.content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

// Convenience: container plus a centred label in one call.
template <class Painter>
inline void drawTextButton(Painter& p, const Rect& r, ButtonVariant v,
                           const ThemeTokens& t, const Interaction& in,
                           const std::string& label, float alpha = 1.0f,
                           int elevation = 0) {
    Color4 content = drawButton(p, r, v, t, in, alpha, elevation);
    p.addTextCenteredIn(r.x, r.y, r.w, r.h, label, content);
}

// Icon buttons are square and circular; the caller draws the glyph itself.
template <class Painter>
inline Color4 drawIconButton(Painter& p, const Rect& r, ButtonVariant v,
                             const ThemeTokens& t, const Interaction& in,
                             float alpha = 1.0f) {
    return drawButton(p, r, v, t, in, alpha, 0);
}

} // namespace material
