#pragma once

// Progress indicators, linear and circular, determinate and indeterminate.
//
// Material's current indicators leave a gap between the active and track
// portions and round both ends, which is what stops them reading as a plain
// loading bar.

#include "anim.h"
#include "state.h"

namespace material {

// `phase` advances continuously for the indeterminate variants; pass an
// accumulating time value.
template <class Painter>
inline void drawLinearProgress(Painter& p, Context& ctx, const Rect& r,
                               float fraction, bool indeterminate,
                               float phase, float alpha = 1.0f) {
    const ThemeColors& c = ctx.t().color;
    float radius = r.h * 0.5f;
    float gap = r.h * 0.6f;

    Color4 track = c.secondaryContainer;
    Color4 active = c.primary;

    if (indeterminate) {
        // Two sweeps out of phase, the shape Material uses so the bar never
        // looks stalled.
        float seg = r.w * 0.35f;
        for (int i = 0; i < 2; ++i) {
            float o = std::fmod(phase * 0.9f + (float)i * 0.5f, 1.4f) - 0.2f;
            float x = r.x + o * r.w;
            float w = seg;
            if (x < r.x) { w -= (r.x - x); x = r.x; }
            if (x + w > r.x + r.w) w = r.x + r.w - x;
            if (w <= 0.0f) continue;
            p.addRoundedRect(x, r.y, w, r.h, radius, active.withAlpha(active.a * alpha));
        }
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, track.withAlpha(track.a * 0.35f * alpha));
        return;
    }

    float f = std::clamp(fraction, 0.0f, 1.0f);
    float aw = r.w * f;
    if (aw > 0.0f)
        p.addRoundedRect(r.x, r.y, aw, r.h, radius, active.withAlpha(active.a * alpha));
    float tx = r.x + aw + gap;
    float tw = (r.x + r.w) - tx;
    if (tw > 0.0f)
        p.addRoundedRect(tx, r.y, tw, r.h, radius, track.withAlpha(track.a * alpha));
}

// Approximated with short segments around the arc, since the painter contract
// only guarantees rects.
template <class Painter>
inline void drawCircularProgress(Painter& p, Context& ctx, const Rect& r,
                                 float fraction, bool indeterminate,
                                 float phase, float alpha = 1.0f) {
    const ThemeColors& c = ctx.t().color;
    float d = std::min(r.w, r.h);
    float cx = r.centerX(), cy = r.centerY();
    float radius = d * 0.5f;
    float thick = std::max(2.0f, d * 0.1f);
    float rr = radius - thick * 0.5f;

    const int segments = 48;
    float start = indeterminate ? std::fmod(phase * 1.6f, 6.2831853f) : -1.5707963f;
    float sweep = indeterminate
        ? (1.2f + 0.9f * std::sin(phase * 1.1f))
        : 6.2831853f * std::clamp(fraction, 0.0f, 1.0f);

    Color4 track = c.secondaryContainer.withAlpha(c.secondaryContainer.a * alpha);
    Color4 active = c.primary.withAlpha(c.primary.a * alpha);

    for (int i = 0; i < segments; ++i) {
        float a = -1.5707963f + 6.2831853f * ((float)i / (float)segments);
        float x = cx + std::cos(a) * rr - thick * 0.5f;
        float y = cy + std::sin(a) * rr - thick * 0.5f;
        p.addRoundedRect(x, y, thick, thick, thick * 0.5f, track);
    }
    int lit = (int)std::round((sweep / 6.2831853f) * (float)segments);
    for (int i = 0; i < lit; ++i) {
        float a = start + 6.2831853f * ((float)i / (float)segments);
        float x = cx + std::cos(a) * rr - thick * 0.5f;
        float y = cy + std::sin(a) * rr - thick * 0.5f;
        p.addRoundedRect(x, y, thick, thick, thick * 0.5f, active);
    }
}

} // namespace material
