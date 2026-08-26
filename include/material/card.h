#pragma once

// Cards: elevated, filled and outlined, in the standard colour roles.

#include "anim.h"
#include "state.h"

namespace material {

enum class CardVariant { Elevated, Filled, Outlined };
enum class CardColor { Surface, Primary, Secondary, Tertiary };

inline Pair cardRole(CardColor v, const ThemeColors& c) {
    switch (v) {
        case CardColor::Surface:   return { c.surfaceContainerLow,  c.onSurface };
        case CardColor::Primary:   return { c.primaryContainer,     c.onPrimaryContainer };
        case CardColor::Secondary: return { c.secondaryContainer,   c.onSecondaryContainer };
        case CardColor::Tertiary:  return { c.tertiaryContainer,    c.onTertiaryContainer };
    }
    return { c.surfaceContainerLow, c.onSurface };
}

// Interactive cards lift on hover, which is the only cue that they are clickable.
template <class Painter>
inline Color4 drawCard(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                       CardVariant variant, CardColor color,
                       const Interaction& in, bool interactive = false,
                       float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    Pair role = cardRole(color, c);
    WidgetState& w = ctx.state(id, in);

    float radius = t.shape.medium;

    int base = (variant == CardVariant::Elevated) ? 1 : 0;
    float lift = interactive ? w.hover.value * (1.0f - w.press.value * 0.5f) : 0.0f;
    int level = std::min(5, base + (int)std::round(lift * 2.0f));
    if (level > 0) drawElevation(p, r, radius, t, level, alpha);

    Color4 fill = role.container;
    if (variant == CardVariant::Elevated && color == CardColor::Surface)
        fill = c.surfaceContainerLow;
    if (interactive) fill = withState(fill, role.content, t.state, in);
    p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));

    if (variant == CardVariant::Outlined)
        p.addRoundedBorder(r.x, r.y, r.w, r.h, radius, 1.0f,
                           c.outlineVariant.withAlpha(c.outlineVariant.a * alpha));

    return role.content.withAlpha(role.content.a * alpha);
}

} // namespace material
