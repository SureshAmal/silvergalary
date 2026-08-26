#pragma once

// Sliders.
//
// Material's current slider is not a thin line with a round thumb: the active
// track is a thick rounded bar, the thumb is a tall pill that grows while
// dragged, and there is a visible gap between thumb and track. Variants add a
// value indicator above (or below) the thumb, and an inset leading icon.

#include "anim.h"
#include "state.h"

namespace material {

enum class SliderIndicator { None, Above, Below };

struct SliderStyle {
    Size size = Size::Small;
    SliderIndicator indicator = SliderIndicator::None;
    bool vertical = false;
    bool centered = false;      // fill outward from the midpoint
};

inline float sliderTrackThickness(Size s) {
    switch (s) {
        case Size::Tiny:   return 16.0f;
        case Size::Small:  return 24.0f;
        case Size::Medium: return 40.0f;
        case Size::Large:  return 56.0f;
        case Size::Extra:  return 96.0f;
    }
    return 24.0f;
}

// Where the thumb sits for a value in 0..1, in the slider's long axis.
inline float sliderThumbPos(const Rect& r, const SliderStyle& st, float frac, float thumbW) {
    float span = (st.vertical ? r.h : r.w) - thumbW;
    float base = st.vertical ? r.y : r.x;
    return base + span * std::clamp(frac, 0.0f, 1.0f);
}

// Draws track, active fill and thumb. Returns the thumb rect so the caller can
// place a value label or hit-test it.
template <class Painter>
inline Rect drawSlider(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                       float frac, const SliderStyle& st,
                       const Interaction& in, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    // The displayed value trails the requested one, so a jump to a new position
    // glides instead of teleporting.
    w.value.step(std::clamp(frac, 0.0f, 1.0f), t.motion.spatialDefault, ctx.dt);
    float v = std::clamp(w.value.value, 0.0f, 1.0f);

    float thick = st.vertical ? r.w : r.h;
    float trackR = thick * 0.5f;

    // Thumb widens on hover and again while dragged - Material's grab affordance.
    float grow = std::max(w.hover.value * 0.35f, w.press.value);
    float thumbW = std::max(4.0f, thick * (0.16f + 0.10f * grow));
    float gap = thick * 0.14f;

    Color4 inactive = applyDisabled(c.surfaceContainerHighest, in.disabled, false);
    Color4 active   = applyDisabled(c.primary, in.disabled, false);
    Color4 thumbCol = applyDisabled(c.primary, in.disabled, false);

    if (!st.vertical) {
        float tx = sliderThumbPos(r, st, v, thumbW);
        float activeStart = st.centered ? r.x + r.w * 0.5f : r.x;
        float activeEnd = tx;
        if (activeEnd < activeStart) std::swap(activeStart, activeEnd);

        // Inactive remainder, then the active bar, both leaving room for the gap.
        p.addRoundedRect(tx + thumbW + gap, r.y, std::max(0.0f, r.x + r.w - tx - thumbW - gap),
                         r.h, trackR, inactive.withAlpha(inactive.a * alpha));
        if (st.centered && activeStart > r.x)
            p.addRoundedRect(r.x, r.y, activeStart - r.x, r.h, trackR,
                             inactive.withAlpha(inactive.a * alpha));
        float aw = std::max(0.0f, activeEnd - activeStart - gap);
        if (aw > 0.0f)
            p.addRoundedRect(activeStart, r.y, aw, r.h, trackR,
                             active.withAlpha(active.a * alpha));

        Rect thumb{ tx, r.y - r.h * 0.06f, thumbW, r.h * 1.12f };
        p.addRoundedRect(thumb.x, thumb.y, thumb.w, thumb.h, thumb.w * 0.5f,
                         thumbCol.withAlpha(thumbCol.a * alpha));
        return thumb;
    }

    float ty = sliderThumbPos(r, st, 1.0f - v, thumbW);
    p.addRoundedRect(r.x, r.y, r.w, std::max(0.0f, ty - r.y - gap), trackR,
                     inactive.withAlpha(inactive.a * alpha));
    float below = (r.y + r.h) - (ty + thumbW + gap);
    if (below > 0.0f)
        p.addRoundedRect(r.x, ty + thumbW + gap, r.w, below, trackR,
                         active.withAlpha(active.a * alpha));
    Rect thumb{ r.x - r.w * 0.06f, ty, r.w * 1.12f, thumbW };
    p.addRoundedRect(thumb.x, thumb.y, thumb.w, thumb.h, thumb.h * 0.5f,
                     thumbCol.withAlpha(thumbCol.a * alpha));
    return thumb;
}

// The value bubble that rides above (or below) the thumb while dragging.
template <class Painter>
inline void drawSliderIndicator(Painter& p, Context& ctx, const Rect& thumb,
                                const std::string& label, SliderIndicator where,
                                float visibility, float alpha = 1.0f) {
    if (where == SliderIndicator::None || visibility <= 0.01f) return;
    const ThemeColors& c = ctx.t().color;

    float pad = 10.0f * ctx.scale;
    float h = p.textHeight() + pad;
    float wdt = p.measureText(label) + pad * 2.0f;
    float x = thumb.centerX() - wdt * 0.5f;
    float y = (where == SliderIndicator::Above) ? thumb.y - h - 6.0f * ctx.scale
                                                : thumb.y + thumb.h + 6.0f * ctx.scale;

    float a = alpha * visibility;
    p.addRoundedRect(x, y, wdt, h, h * 0.5f, c.inverseSurface.withAlpha(c.inverseSurface.a * a));
    p.addTextCenteredIn(x, y, wdt, h, label, c.inverseOnSurface.withAlpha(c.inverseOnSurface.a * a));
}

// Value for a pointer position along the slider, for drag handling.
inline float sliderValueAt(const Rect& r, const SliderStyle& st, float px, float py) {
    if (st.vertical) {
        float f = (r.h > 0.0f) ? (py - r.y) / r.h : 0.0f;
        return std::clamp(1.0f - f, 0.0f, 1.0f);
    }
    return std::clamp((r.w > 0.0f) ? (px - r.x) / r.w : 0.0f, 0.0f, 1.0f);
}

} // namespace material
