#pragma once

// Toolbars and button groups.
//
// A toolbar is a floating stadium container holding icon buttons, one of which
// may be active. Button groups come in two flavours: standard (separate,
// rounded) and connected (touching, square inner edges) - plus the split button,
// where a primary action and a dropdown share one container.

#include "anim.h"
#include "state.h"
#include <vector>

namespace material {

enum class ToolbarColor { Surface, Primary, Secondary, Tertiary };

inline Pair toolbarRole(ToolbarColor v, const ThemeColors& c) {
    switch (v) {
        case ToolbarColor::Surface:   return { c.surfaceContainer,     c.onSurface };
        case ToolbarColor::Primary:   return { c.primaryContainer,     c.onPrimaryContainer };
        case ToolbarColor::Secondary: return { c.secondaryContainer,   c.onSecondaryContainer };
        case ToolbarColor::Tertiary:  return { c.tertiaryContainer,    c.onTertiaryContainer };
    }
    return { c.surfaceContainer, c.onSurface };
}

template <class Painter>
inline Color4 drawToolbar(Painter& p, Context& ctx, const Rect& r,
                          ToolbarColor variant, Elevate elevate = Elevate::Small,
                          bool vertical = false, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    Pair role = toolbarRole(variant, t.color);
    float radius = (vertical ? r.w : r.h) * 0.5f;
    if ((int)elevate > 0) drawElevation(p, r, radius, t, (int)elevate, alpha);
    p.addRoundedRect(r.x, r.y, r.w, r.h, radius,
                     role.container.withAlpha(role.container.a * alpha));
    return role.content.withAlpha(role.content.a * alpha);
}

// One slot inside a toolbar. Active slots get a filled pill behind them.
template <class Painter>
inline Color4 drawToolbarItem(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                              const Interaction& in, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    float on = std::clamp(w.selected.value, 0.0f, 1.0f);
    float layer = stateOpacity(t.state, in);
    float radius = std::min(r.w, r.h) * 0.5f;

    if (on > 0.01f || layer > 0.0f) {
        Color4 fill = Color4::mix(Color4(c.onSurface.r, c.onSurface.g, c.onSurface.b, layer),
                                  c.secondaryContainer, on);
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));
    }
    Color4 content = Color4::mix(c.onSurfaceVariant, c.onSecondaryContainer, on);
    content = applyDisabled(content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

// ---------------------------------------------------------------------------
// Button groups
// ---------------------------------------------------------------------------

struct GroupItem {
    Rect rect;
    Interaction in;
};

// Standard: separate buttons with a gap between them.
inline std::vector<GroupItem> layoutGroup(const Rect& r, int count, float gap) {
    std::vector<GroupItem> out;
    if (count <= 0) return out;
    out.resize((size_t)count);
    float w = (r.w - gap * (float)(count - 1)) / (float)count;
    for (int i = 0; i < count; ++i)
        out[(size_t)i].rect = Rect{ r.x + (w + gap) * (float)i, r.y, w, r.h };
    return out;
}

template <class Painter>
inline Color4 drawGroupItem(Painter& p, Context& ctx, uint64_t id, const GroupItem& it,
                            bool connected, bool first, bool last, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, it.in);

    float on = std::clamp(w.selected.value, 0.0f, 1.0f);
    const Rect& r = it.rect;
    float full = r.h * 0.5f;
    // A connected group rounds only its outer ends; a standard one rounds every
    // button. Selection also pushes the corners rounder, which is Material's
    // shape-morph cue that this item is the active one.
    float radius = connected ? full : std::min(t.shape.medium + (full - t.shape.medium) * on, full);

    Pair role = on > 0.5f ? roleSelected(c) : Pair{ c.surfaceContainerHigh, c.onSurfaceVariant };
    Color4 fill = withState(role.container, role.content, t.state, it.in);
    fill = applyDisabled(fill, it.in.disabled, false);
    fill = fill.withAlpha(fill.a * alpha);

    if (connected && !(first && last)) {
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill);
        if (!last)  p.addRect(r.x + r.w - radius, r.y, radius, r.h, fill);
        if (!first) p.addRect(r.x, r.y, radius, r.h, fill);
    } else {
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill);
    }

    Color4 content = applyDisabled(role.content, it.in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

// Split button: a wide primary action plus a narrow dropdown, sharing a
// container but separated by a gap so both halves are independently clickable.
struct SplitButton {
    Rect main;
    Rect trailing;
};

inline SplitButton layoutSplitButton(const Rect& r, float scale) {
    float trailW = 40.0f * scale;
    float gap = 2.0f * scale;
    SplitButton s;
    s.main = Rect{ r.x, r.y, std::max(0.0f, r.w - trailW - gap), r.h };
    s.trailing = Rect{ r.x + r.w - trailW, r.y, trailW, r.h };
    return s;
}

} // namespace material
