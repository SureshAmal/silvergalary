#pragma once

// Segmented button.
//
// One connected container with a shared outline and hairline dividers - not a
// row of separate chips. Outer ends are stadium, inner edges square, and the
// selected segment is filled with secondaryContainer.

#include "state.h"
#include <vector>

namespace material {

struct Segment {
    Rect rect;
    Interaction in;
};

// Lay out `count` segments across the rect. `naturalWidths`, when supplied and
// correctly sized, gives each segment its content width with slack shared
// evenly; otherwise segments are equal.
inline std::vector<Segment> layoutSegments(const Rect& r,
                                           const std::vector<float>* naturalWidths,
                                           int count) {
    std::vector<Segment> out;
    if (count <= 0 || r.w <= 0.0f) return out;
    out.resize((size_t)count);

    float total = 0.0f;
    if (naturalWidths && (int)naturalWidths->size() == count)
        for (float nw : *naturalWidths) total += nw;

    float cursor = r.x;
    for (int i = 0; i < count; ++i) {
        float segW = (total > 0.0f)
            ? (*naturalWidths)[(size_t)i] + std::max(0.0f, r.w - total) / (float)count
            : r.w / (float)count;
        // Absorb rounding into the last segment so the group ends exactly at r.x+r.w.
        if (i == count - 1) segW = std::max(0.0f, (r.x + r.w) - cursor);
        out[(size_t)i].rect = Rect{ cursor, r.y, segW, r.h };
        cursor += segW;
    }
    return out;
}

template <class Painter>
inline void drawSegmentedButton(Painter& p, const std::vector<Segment>& segs,
                                const ThemeTokens& t, float alpha = 1.0f) {
    if (segs.empty()) return;
    const ThemeColors& c = t.color;
    const float h = segs.front().rect.h;
    const float radius = h * 0.5f;
    const float x = segs.front().rect.x;
    const float y = segs.front().rect.y;
    const float w = (segs.back().rect.x + segs.back().rect.w) - x;

    p.addRoundedBorder(x, y, w, h, radius, std::max(1.0f, h * 0.045f),
                       c.outline.withAlpha(c.outline.a * alpha));

    for (size_t i = 0; i < segs.size(); ++i) {
        const Segment& s = segs[i];
        Pair role = s.in.selected ? roleSelected(c) : roleTransparent(c);
        Color4 fill = withState(role.container, role.content, t.state, s.in);
        if (fill.a <= 0.001f) continue;
        fill = fill.withAlpha(fill.a * alpha);

        // Only the outer ends round; interior edges stay square so the group
        // reads as one control rather than adjacent pills.
        bool first = (i == 0), last = (i + 1 == segs.size());
        const Rect& r = s.rect;
        if (first || last) {
            p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill);
            if (!last)  p.addRect(r.x + r.w - radius, r.y, radius, r.h, fill);
            if (!first) p.addRect(r.x, r.y, radius, r.h, fill);
        } else {
            p.addRect(r.x, r.y, r.w, r.h, fill);
        }
    }

    for (size_t i = 1; i < segs.size(); ++i) {
        p.addRect(segs[i].rect.x, y, std::max(1.0f, h * 0.03f), h,
                  c.outline.withAlpha(c.outline.a * 0.6f * alpha));
    }
}

inline Color4 segmentContent(const Segment& s, const ThemeColors& c, float alpha = 1.0f) {
    Color4 col = s.in.selected ? roleSelected(c).content : c.onSurfaceVariant;
    return col.withAlpha(col.a * alpha);
}

template <class Painter>
inline void drawSegmentedLabels(Painter& p, const std::vector<Segment>& segs,
                                const std::vector<std::string>& labels,
                                const ThemeTokens& t, float alpha = 1.0f) {
    size_t n = std::min(segs.size(), labels.size());
    for (size_t i = 0; i < n; ++i) {
        const Rect& r = segs[i].rect;
        p.addTextCenteredIn(r.x, r.y, r.w, r.h, labels[i],
                            segmentContent(segs[i], t.color, alpha));
    }
}

} // namespace material
