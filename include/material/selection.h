#pragma once

// Selection controls: switch, checkbox, radio.
//
// All three animate their transition rather than snapping, which is most of what
// makes them read as Material: the switch knob slides and grows, the checkbox
// box fills and its tick draws in, the radio's inner dot scales up.

#include "anim.h"
#include "state.h"

namespace material {

// ---------------------------------------------------------------------------
// Switch
// ---------------------------------------------------------------------------

// Material's switch: an off knob is small and sits on outline; an on knob is
// larger, takes onPrimary, and rides a primary track. Both ends animate.
template <class Painter>
inline void drawSwitch(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                       const Interaction& in, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    float on = std::clamp(w.selected.value, 0.0f, 1.0f);
    float trackR = r.h * 0.5f;

    Color4 trackOff = c.surfaceContainerHighest;
    Color4 trackOn  = c.primary;
    Color4 track = Color4::mix(trackOff, trackOn, on);
    track = applyDisabled(track, in.disabled, false);
    p.addRoundedRect(r.x, r.y, r.w, r.h, trackR, track.withAlpha(track.a * alpha));

    // The outline fades out as the track fills, so the two never fight.
    if (on < 0.999f) {
        Color4 line = c.outline.withAlpha(c.outline.a * (1.0f - on) * alpha);
        p.addRoundedBorder(r.x, r.y, r.w, r.h, trackR, std::max(1.0f, r.h * 0.08f), line);
    }

    // Knob grows from 60% to 75% of the track height, and grows again on press.
    float knobOff = r.h * 0.60f, knobOn = r.h * 0.75f;
    float knob = knobOff + (knobOn - knobOff) * on;
    knob += (r.h * 0.08f) * std::clamp(w.press.value, 0.0f, 1.0f);
    knob = std::min(knob, r.h - 2.0f);

    float inset = (r.h - knob) * 0.5f;
    float travel = r.w - knob - inset * 2.0f;
    float kx = r.x + inset + travel * on;

    Color4 knobCol = Color4::mix(c.outline, c.onPrimary, on);
    knobCol = applyDisabled(knobCol, in.disabled, false);
    p.addRoundedRect(kx, r.y + inset, knob, knob, knob * 0.5f,
                     knobCol.withAlpha(knobCol.a * alpha));

    // Hover/focus halo behind the knob.
    float halo = std::max(w.hover.value, w.focus.value);
    if (halo > 0.01f && !in.disabled) {
        float hr = knob * 1.9f;
        Color4 hc = Color4::mix(c.onSurface, c.primary, on);
        p.addRoundedRect(kx + knob * 0.5f - hr * 0.5f, r.y + r.h * 0.5f - hr * 0.5f,
                         hr, hr, hr * 0.5f,
                         hc.withAlpha(t.state.hover * halo * alpha));
    }
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

// The tick is drawn as two strokes whose lengths animate in, so the mark appears
// to be written rather than to pop.
template <class Painter>
inline void drawCheckbox(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                         const Interaction& in, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    float on = std::clamp(w.selected.value, 0.0f, 1.0f);
    float box = std::min(r.w, r.h);
    float bx = r.x + (r.w - box) * 0.5f;
    float by = r.y + (r.h - box) * 0.5f;
    float radius = std::min(t.shape.extraSmall, box * 0.3f);

    float halo = std::max(w.hover.value, std::max(w.focus.value, w.press.value));
    if (halo > 0.01f && !in.disabled) {
        float hr = box * 2.0f;
        Color4 hc = on > 0.5f ? c.primary : c.onSurface;
        p.addRoundedRect(bx + box * 0.5f - hr * 0.5f, by + box * 0.5f - hr * 0.5f,
                         hr, hr, hr * 0.5f, hc.withAlpha(t.state.hover * halo * alpha));
    }

    Color4 fill = applyDisabled(c.primary, in.disabled, false);
    if (on > 0.01f)
        p.addRoundedRect(bx, by, box, box, radius, fill.withAlpha(fill.a * on * alpha));
    if (on < 0.999f) {
        Color4 line = applyDisabled(c.onSurfaceVariant, in.disabled, false);
        p.addRoundedBorder(bx, by, box, box, radius, std::max(1.0f, box * 0.09f),
                           line.withAlpha(line.a * (1.0f - on) * alpha));
    }

    if (on > 0.02f) {
        Color4 mark = applyDisabled(c.onPrimary, in.disabled, true);
        mark = mark.withAlpha(mark.a * alpha);
        float th = std::max(1.5f, box * 0.12f);
        // Short stroke completes over the first 40% of the animation, the long
        // one over the rest, which is the order a tick is actually written in.
        float a1 = std::clamp(on / 0.4f, 0.0f, 1.0f);
        float a2 = std::clamp((on - 0.4f) / 0.6f, 0.0f, 1.0f);
        float cx = bx + box * 0.28f, cy = by + box * 0.52f;
        float len1 = box * 0.22f * a1;
        for (float s = 0.0f; s < len1; s += th * 0.5f)
            p.addRoundedRect(cx + s, cy + s, th, th, th * 0.5f, mark);
        float px = bx + box * 0.46f, py = by + box * 0.70f;
        float len2 = box * 0.34f * a2;
        for (float s = 0.0f; s < len2; s += th * 0.5f)
            p.addRoundedRect(px + s, py - s, th, th, th * 0.5f, mark);
    }
}

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------

template <class Painter>
inline void drawRadio(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                      const Interaction& in, float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    float on = std::clamp(w.selected.value, 0.0f, 1.0f);
    float box = std::min(r.w, r.h);
    float bx = r.x + (r.w - box) * 0.5f;
    float by = r.y + (r.h - box) * 0.5f;

    float halo = std::max(w.hover.value, std::max(w.focus.value, w.press.value));
    if (halo > 0.01f && !in.disabled) {
        float hr = box * 2.0f;
        Color4 hc = on > 0.5f ? c.primary : c.onSurface;
        p.addRoundedRect(bx + box * 0.5f - hr * 0.5f, by + box * 0.5f - hr * 0.5f,
                         hr, hr, hr * 0.5f, hc.withAlpha(t.state.hover * halo * alpha));
    }

    Color4 ring = applyDisabled(on > 0.5f ? c.primary : c.onSurfaceVariant, in.disabled, false);
    p.addRoundedBorder(bx, by, box, box, box * 0.5f, std::max(1.0f, box * 0.09f),
                       ring.withAlpha(ring.a * alpha));

    if (on > 0.01f) {
        float dot = box * 0.5f * on;
        Color4 fill = applyDisabled(c.primary, in.disabled, false);
        p.addRoundedRect(bx + (box - dot) * 0.5f, by + (box - dot) * 0.5f,
                         dot, dot, dot * 0.5f, fill.withAlpha(fill.a * alpha));
    }
}

} // namespace material
