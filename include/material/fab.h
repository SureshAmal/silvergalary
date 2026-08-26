#pragma once

// Floating action buttons: standard, small/large/extra, and extended.
//
// A FAB is a tertiaryContainer-family surface with real elevation, and it lifts
// further on hover - the lift is the point, so it animates.

#include "anim.h"
#include "state.h"

namespace material {

enum class FabColor { Primary, Secondary, Tertiary, Surface };

inline Pair fabRole(FabColor v, const ThemeColors& c) {
    switch (v) {
        case FabColor::Primary:   return { c.primaryContainer,   c.onPrimaryContainer };
        case FabColor::Secondary: return { c.secondaryContainer, c.onSecondaryContainer };
        case FabColor::Tertiary:  return { c.tertiaryContainer,  c.onTertiaryContainer };
        case FabColor::Surface:   return { c.surfaceContainerHigh, c.primary };
    }
    return { c.primaryContainer, c.onPrimaryContainer };
}

inline float fabSize(Size s) {
    switch (s) {
        case Size::Tiny:   return 40.0f;
        case Size::Small:  return 40.0f;
        case Size::Medium: return 56.0f;
        case Size::Large:  return 96.0f;
        case Size::Extra:  return 124.0f;
    }
    return 56.0f;
}

inline float fabRadius(Size s, const ThemeShape& sh) {
    switch (s) {
        case Size::Tiny:
        case Size::Small:  return sh.medium;
        case Size::Medium: return sh.large;
        case Size::Large:  return sh.extraLarge;
        case Size::Extra:  return sh.extraLargeIncreased;
    }
    return sh.large;
}

// Returns the content colour for the caller's icon or label.
template <class Painter>
inline Color4 drawFab(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                      FabColor variant, Size size, const Interaction& in,
                      Elevate elevate = Elevate::Medium, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    Pair role = fabRole(variant, t.color);
    WidgetState& w = ctx.state(id, in);

    float radius = fabRadius(size, t.shape);
    radius = t.shape.morph(std::min(radius, std::min(r.w, r.h) * 0.5f),
                           std::clamp(w.press.value, 0.0f, 1.0f));

    // Hover raises the FAB one level; press settles it back down.
    int base = (int)elevate;
    float lift = w.hover.value * (1.0f - w.press.value);
    int level = std::min(5, base + (int)std::round(lift));
    if (level > 0) drawElevation(p, r, radius, t, level, alpha);

    Color4 fill = withState(role.container, role.content, t.state, in);
    fill = applyDisabled(fill, in.disabled, false);
    p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));

    Color4 content = applyDisabled(role.content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

// Extended FAB: a stadium pill carrying an icon and a label.
template <class Painter>
inline Color4 drawExtendedFab(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                              FabColor variant, const Interaction& in,
                              Elevate elevate = Elevate::Medium, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    Pair role = fabRole(variant, t.color);
    WidgetState& w = ctx.state(id, in);

    float radius = t.shape.morph(std::min(t.shape.large, r.h * 0.5f),
                                 std::clamp(w.press.value, 0.0f, 1.0f));
    int level = std::min(5, (int)elevate + (int)std::round(w.hover.value * (1.0f - w.press.value)));
    if (level > 0) drawElevation(p, r, radius, t, level, alpha);

    Color4 fill = withState(role.container, role.content, t.state, in);
    fill = applyDisabled(fill, in.disabled, false);
    p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));

    Color4 content = applyDisabled(role.content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

} // namespace material
