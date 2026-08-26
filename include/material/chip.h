#pragma once

// Chips: assist, filter and input.
//
// A chip is a stadium container with an outline when unselected and a tonal
// fill when selected - the filter chip's selected state is what most UIs get
// wrong by reaching for `primary`.

#include "state.h"

namespace material {

enum class ChipVariant { Assist, Filter, Input, Suggestion };

template <class Painter>
inline Color4 drawChip(Painter& p, const Rect& r, ChipVariant v,
                       const ThemeTokens& t, const Interaction& in,
                       float alpha = 1.0f) {
    const ThemeColors& c = t.color;
    bool selected = in.selected;

    Pair role = selected ? roleSelected(c)
                         : Pair{ Color4(0,0,0,0), c.onSurfaceVariant };

    float radius = t.shape.morph(std::min(t.shape.small, std::min(r.w, r.h) * 0.5f),
                                 in.pressed ? 1.0f : 0.0f);

    Color4 fill = withState(role.container, role.content, t.state, in);
    fill = applyDisabled(fill, in.disabled, false);
    if (fill.a > 0.001f)
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));

    // Unselected chips carry an outline; selected ones rely on their fill.
    if (!selected && v != ChipVariant::Suggestion) {
        Color4 line = applyDisabled(c.outline, in.disabled, false);
        p.addRoundedBorder(r.x, r.y, r.w, r.h, radius,
                           std::max(1.0f, r.h * 0.04f), line.withAlpha(line.a * alpha));
    }

    Color4 content = applyDisabled(role.content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

} // namespace material
