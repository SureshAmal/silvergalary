#pragma once

// Badges: the small dot or count that rides on a host element's corner, and the
// inline variant that sits beside a label inside a button or chip.

#include "anim.h"
#include "state.h"

namespace material {

enum class BadgeCorner { TopRight, TopLeft, BottomLeft, BottomRight };

struct BadgeStyle {
    BadgeCorner corner = BadgeCorner::TopRight;
    Pair role;                 // container + content; default is error
    bool useDefaultRole = true;
};

inline Pair badgeRole(const BadgeStyle& st, const ThemeColors& c) {
    return st.useDefaultRole ? Pair{ c.error, c.onError } : st.role;
}

// A badge with no label is a 6pt dot; with a label it grows to a pill and its
// count is clamped, since a badge is a glance not a readout.
template <class Painter>
inline void drawBadge(Painter& p, Context& ctx, const Rect& host,
                      const std::string& label, const BadgeStyle& st,
                      float alpha = 1.0f) {
    const ThemeColors& c = ctx.t().color;
    Pair role = badgeRole(st, c);

    float k = ctx.scale;
    float h, w;
    if (label.empty()) {
        h = w = 6.0f * k;
    } else {
        h = 16.0f * k;
        w = std::max(h, p.measureText(label) + 8.0f * k);
    }

    float x = host.x + host.w - w * 0.5f;
    float y = host.y - h * 0.5f;
    switch (st.corner) {
        case BadgeCorner::TopRight:    break;
        case BadgeCorner::TopLeft:     x = host.x - w * 0.5f; break;
        case BadgeCorner::BottomLeft:  x = host.x - w * 0.5f;
                                       y = host.y + host.h - h * 0.5f; break;
        case BadgeCorner::BottomRight: y = host.y + host.h - h * 0.5f; break;
    }

    p.addRoundedRect(x, y, w, h, h * 0.5f, role.container.withAlpha(role.container.a * alpha));
    if (!label.empty())
        p.addTextCenteredIn(x, y, w, h, label, role.content.withAlpha(role.content.a * alpha));
}

// Sits inside a host, after its label.
template <class Painter>
inline float drawInlineBadge(Painter& p, Context& ctx, float x, float centerY,
                             const std::string& label, const BadgeStyle& st,
                             float alpha = 1.0f) {
    const ThemeColors& c = ctx.t().color;
    Pair role = badgeRole(st, c);
    float k = ctx.scale;
    float h = 18.0f * k;
    float w = std::max(h, p.measureText(label) + 10.0f * k);
    float y = centerY - h * 0.5f;
    p.addRoundedRect(x, y, w, h, h * 0.5f, role.container.withAlpha(role.container.a * alpha));
    p.addTextCenteredIn(x, y, w, h, label, role.content.withAlpha(role.content.a * alpha));
    return w;
}

} // namespace material
