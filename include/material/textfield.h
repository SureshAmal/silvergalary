#pragma once

// Text fields: outlined, filled and rounded, with label, helper and error text
// and optional leading/trailing icons.
//
// The label animates from resting inside the field to floating on its border
// once the field is focused or non-empty - that motion is the component's whole
// identity, so it is spring-driven rather than toggled.

#include "anim.h"
#include "state.h"

namespace material {

enum class FieldVariant { Outlined, Filled, Rounded };

struct FieldStyle {
    FieldVariant variant = FieldVariant::Outlined;
    Size size = Size::Medium;
    bool error = false;
    bool hasLeadingIcon = false;
    bool hasTrailingIcon = false;
};

struct FieldMetrics {
    Rect content;       // where the text itself goes
    Rect leadingIcon;
    Rect trailingIcon;
    float radius = 0.0f;
};

inline FieldMetrics fieldMetrics(const Rect& r, const FieldStyle& st,
                                 const ThemeTokens& t, float scale) {
    FieldMetrics m;
    float pad = 16.0f * scale;
    float icon = 24.0f * scale;
    switch (st.variant) {
        case FieldVariant::Outlined: m.radius = t.shape.extraSmall; break;
        case FieldVariant::Filled:   m.radius = t.shape.extraSmall; break;
        case FieldVariant::Rounded:  m.radius = r.h * 0.5f; break;
    }
    float left = r.x + pad, right = r.x + r.w - pad;
    if (st.hasLeadingIcon) {
        m.leadingIcon = Rect{ left, r.y + (r.h - icon) * 0.5f, icon, icon };
        left += icon + 12.0f * scale;
    }
    if (st.hasTrailingIcon) {
        right -= icon;
        m.trailingIcon = Rect{ right, r.y + (r.h - icon) * 0.5f, icon, icon };
        right -= 12.0f * scale;
    }
    m.content = Rect{ left, r.y, std::max(0.0f, right - left), r.h };
    return m;
}

// Draws the container and the label in its animated position. Returns the colour
// for the field's own text.
template <class Painter>
inline Color4 drawTextField(Painter& p, Context& ctx, uint64_t id, const Rect& r,
                            const FieldStyle& st, const std::string& label,
                            bool hasContent, const Interaction& in,
                            float alpha = 1.0f) {
    const ThemeTokens& t = ctx.t();
    const ThemeColors& c = t.color;
    WidgetState& w = ctx.state(id, in);

    FieldMetrics m = fieldMetrics(r, st, t, ctx.scale);

    // Label floats when focused or filled; the spring carries it between.
    w.value.step((in.focused || hasContent) ? 1.0f : 0.0f, t.motion.spatialFast, ctx.dt);
    float lift = std::clamp(w.value.value, 0.0f, 1.0f);

    Color4 accent = st.error ? c.error : c.primary;
    Color4 line = st.error ? c.error
                           : Color4::mix(c.outline, c.primary, std::max(lift * 0.0f, w.focus.value));
    line = applyDisabled(line, in.disabled, false);

    if (st.variant == FieldVariant::Filled) {
        Color4 fill = withState(c.surfaceContainerHighest, c.onSurface, t.state, in);
        p.addRoundedRect(r.x, r.y, r.w, r.h, m.radius, fill.withAlpha(fill.a * alpha));
        p.addRect(r.x, r.y + r.h - 2.0f * ctx.scale, r.w,
                  std::max(1.0f, (1.0f + w.focus.value) * ctx.scale),
                  line.withAlpha(line.a * alpha));
    } else {
        if (w.hover.value > 0.01f && !in.disabled) {
            Color4 hov = c.onSurface.withAlpha(t.state.hover * w.hover.value * 0.5f * alpha);
            p.addRoundedRect(r.x, r.y, r.w, r.h, m.radius, hov);
        }
        // The border thickens as focus arrives rather than switching width.
        p.addRoundedBorder(r.x, r.y, r.w, r.h, m.radius,
                           std::max(1.0f, (1.0f + w.focus.value) * ctx.scale),
                           line.withAlpha(line.a * alpha));
    }

    if (!label.empty()) {
        float restY = r.y;
        float floatY = r.y - p.textHeight() * 0.5f;
        float ly = restY + (floatY - restY) * lift;
        Color4 lc = st.error ? c.error
                             : Color4::mix(c.onSurfaceVariant, accent, w.focus.value);
        lc = applyDisabled(lc, in.disabled, true);

        // Punch a gap in the outline behind a floated label so the two do not
        // overlap - this is why an outlined field's label appears to sit "in" it.
        if (lift > 0.5f && st.variant != FieldVariant::Filled) {
            float lw = p.measureText(label) + 8.0f * ctx.scale;
            p.addRect(m.content.x - 4.0f * ctx.scale, ly + p.textHeight() * 0.15f,
                      lw, 2.0f * ctx.scale, c.surface.withAlpha(alpha));
        }
        if (lift > 0.5f)
            p.addTextVCentered(m.content.x, ly, p.textHeight(), label,
                               lc.withAlpha(lc.a * alpha));
        else
            p.addTextVCentered(m.content.x, r.y, r.h, label, lc.withAlpha(lc.a * alpha * (1.0f - lift)));
    }

    Color4 text = applyDisabled(c.onSurface, in.disabled, true);
    return text.withAlpha(text.a * alpha);
}

// Helper or error text under the field.
template <class Painter>
inline void drawFieldSupport(Painter& p, Context& ctx, const Rect& field,
                             const std::string& text, bool isError, float alpha = 1.0f) {
    if (text.empty()) return;
    const ThemeColors& c = ctx.t().color;
    Color4 col = isError ? c.error : c.onSurfaceVariant;
    p.addTextVCentered(field.x + 16.0f * ctx.scale, field.y + field.h + 2.0f * ctx.scale,
                       p.textHeight() + 6.0f * ctx.scale, text,
                       col.withAlpha(col.a * alpha));
}

} // namespace material
