#pragma once

// The renderer contract.
//
// The component library never names a concrete renderer. Every draw function is
// templated on a Painter and calls only the methods below, so the library can be
// dropped onto any immediate-mode batcher - this app's FontRenderer, a test
// harness that records calls, or something else entirely - by supplying a type
// that implements them.
//
//   void  addRect(float x, float y, float w, float h, Color4 c)
//   void  addRoundedRect(float x, float y, float w, float h, float r, Color4 c)
//   void  addRoundedBorder(float x, float y, float w, float h, float r,
//                          float thickness, Color4 c)
//   void  addTextVCentered(float x, float y, float h, const std::string& s, Color4 c)
//   void  addTextCenteredIn(float x, float y, float w, float h,
//                           const std::string& s, Color4 c)
//   float measureText(const std::string& s)
//   float textHeight()
//
// Implementations are expected to clamp a corner radius to half the shorter
// side, so components can pass ThemeShape::full to mean "stadium".
//
// A Painter is passed by reference and never stored. Nothing here allocates.

#include "tokens.h"
#include <string>

namespace material {

// Geometry every component takes. Kept separate from interaction state so a
// caller can lay out once and draw in several states.
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    Rect inset(float d) const { return Rect{ x + d, y + d, w - d * 2.0f, h - d * 2.0f }; }
    float centerX() const { return x + w * 0.5f; }
    float centerY() const { return y + h * 0.5f; }
};

// What the pointer/keyboard is doing to a component right now.
struct Interaction {
    bool hovered  = false;
    bool pressed  = false;
    bool focused  = false;
    bool selected = false;
    bool disabled = false;
};

// Component size scale. Metrics below are Material's defaults in points, scaled
// by Context::scale at draw time.
enum class Size { Tiny, Small, Medium, Large, Extra };

inline float sizeHeight(Size s) {
    switch (s) {
        case Size::Tiny:   return 32.0f;
        case Size::Small:  return 40.0f;
        case Size::Medium: return 56.0f;
        case Size::Large:  return 96.0f;
        case Size::Extra:  return 136.0f;
    }
    return 56.0f;
}

inline float sizeIcon(Size s) {
    switch (s) {
        case Size::Tiny:   return 20.0f;
        case Size::Small:  return 24.0f;
        case Size::Medium: return 24.0f;
        case Size::Large:  return 32.0f;
        case Size::Extra:  return 40.0f;
    }
    return 24.0f;
}

// Elevation presets components expose as a variant, matching the levels in
// ThemeElevation.
enum class Elevate { None = 0, Small = 1, Medium = 2, Large = 3 };

// Material's disabled treatment: content drops to 38% and any container to 12%,
// rather than each component inventing its own greyed-out colour.
inline Color4 applyDisabled(Color4 c, bool disabled, bool isContent) {
    if (!disabled) return c;
    return c.withAlpha(c.a * (isContent ? 0.38f : 0.12f));
}

} // namespace material
