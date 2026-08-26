#pragma once

// List items and data tables.
//
// A list item is a state layer over a transparent container, or
// secondaryContainer when selected. A table is the same row treatment plus
// column metrics and a header rule.

#include "state.h"
#include <vector>

namespace material {

// Draws the row and returns the colour for its content.
template <class Painter>
inline Color4 drawListItem(Painter& p, const Rect& r, const ThemeTokens& t,
                           const Interaction& in, float radius, float alpha = 1.0f) {
    const ThemeColors& c = t.color;
    Pair role = in.selected ? roleSelected(c) : roleTransparent(c);
    Color4 fill = withState(role.container, role.content, t.state, in);
    if (fill.a > 0.001f)
        p.addRoundedRect(r.x, r.y, r.w, r.h, radius, fill.withAlpha(fill.a * alpha));
    Color4 content = applyDisabled(role.content, in.disabled, true);
    return content.withAlpha(content.a * alpha);
}

// Supporting text sits at onSurfaceVariant, one step down from the headline.
inline Color4 listSupportingColor(const ThemeColors& c, float alpha = 1.0f) {
    return c.onSurfaceVariant.withAlpha(c.onSurfaceVariant.a * alpha);
}

template <class Painter>
inline void drawDivider(Painter& p, float x, float y, float w,
                        const ThemeTokens& t, float thickness = 1.0f, float alpha = 1.0f) {
    p.addRect(x, y, w, thickness,
              t.color.outlineVariant.withAlpha(t.color.outlineVariant.a * alpha));
}

// ---------------------------------------------------------------------------
// Data table
// ---------------------------------------------------------------------------

enum class ColumnAlign { Start, Center, End };

struct Column {
    std::string title;
    float width = 0.0f;          // resolved width in points
    float flex = 0.0f;           // share of leftover space; 0 = fixed
    ColumnAlign align = ColumnAlign::Start;
};

// Resolve flexible column widths against the available width.
inline void layoutColumns(std::vector<Column>& cols, float available, float gap) {
    if (cols.empty()) return;
    float fixed = 0.0f, flexTotal = 0.0f;
    for (const Column& c : cols) {
        if (c.flex > 0.0f) flexTotal += c.flex;
        else               fixed += c.width;
    }
    float slack = std::max(0.0f, available - fixed - gap * (float)(cols.size() - 1));
    for (Column& c : cols)
        if (c.flex > 0.0f && flexTotal > 0.0f) c.width = slack * (c.flex / flexTotal);
}

// x offset for text within a column, given the text width.
inline float columnTextX(const Column& c, float colX, float textW, float pad) {
    switch (c.align) {
        case ColumnAlign::Center: return colX + (c.width - textW) * 0.5f;
        case ColumnAlign::End:    return colX + c.width - textW - pad;
        case ColumnAlign::Start:  break;
    }
    return colX + pad;
}

template <class Painter>
inline void drawTableHeader(Painter& p, const Rect& r,
                            const std::vector<Column>& cols,
                            const ThemeTokens& t, float gap, float pad,
                            float alpha = 1.0f) {
    const ThemeColors& c = t.color;
    Color4 label = c.onSurfaceVariant.withAlpha(c.onSurfaceVariant.a * alpha);
    float x = r.x;
    for (const Column& col : cols) {
        float tw = p.measureText(col.title);
        p.addTextVCentered(columnTextX(col, x, tw, pad), r.y, r.h, col.title, label);
        x += col.width + gap;
    }
    drawDivider(p, r.x, r.y + r.h - 1.0f, r.w, t, 1.0f, alpha);
}

} // namespace material
