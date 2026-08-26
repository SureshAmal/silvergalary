#pragma once

// Animated widget state.
//
// The components are immediate-mode: they are called fresh every frame and hold
// nothing. That is fine for drawing, but an animation needs a value that
// survives between frames - a switch knob mid-slide, a checkbox mid-tick, a
// state layer fading in.
//
// AnimStore keeps that per widget, keyed by an id the caller supplies, and steps
// every channel with the theme's own springs. A widget that stops being drawn is
// eventually dropped, so a long-lived store does not grow without bound.

#include "painter.h"
#include <unordered_map>
#include <cstdint>
#include <cstring>

namespace material {

// Second-order spring, the same model the rest of the app animates with. Kept
// self-contained so the library depends on nothing outside this directory.
struct Spring {
    float value = 0.0f;
    float velocity = 0.0f;

    void snap(float v) { value = v; velocity = 0.0f; }

    void step(float target, const ThemeMotionSpec& spec, float dt) {
        if (dt <= 0.0f) return;
        float f = (spec.frequency > 0.01f) ? spec.frequency : 0.01f;
        const float twoPiF = 6.2831853f * f;
        float k1 = spec.damping / (3.1415927f * f);
        float k2 = 1.0f / (twoPiF * twoPiF);
        float k3 = spec.response * spec.damping / twoPiF;

        // Sub-step so a long frame cannot make the spring diverge. k2 sets the
        // stable step size; this is the standard guard for the explicit solver.
        int steps = 1;
        float stable = 0.8f * (std::sqrt(4.0f * k2 + k1 * k1) - k1);
        if (stable > 0.0f && dt > stable) steps = (int)std::ceil(dt / stable);
        if (steps > 16) steps = 16;
        float h = dt / (float)steps;

        for (int i = 0; i < steps; ++i) {
            float targetVel = 0.0f;   // targets here are steps, not moving points
            value += h * velocity;
            float denom = (k2 > 1e-8f) ? k2 : 1e-8f;
            velocity += h * (target + k3 * targetVel - value - k1 * velocity) / denom;
        }
    }
};

// The channels a component may animate. Not every component uses all of them.
struct WidgetState {
    Spring hover;      // 0..1 state layer
    Spring press;      // 0..1 press / shape morph
    Spring selected;   // 0..1 checked, toggled, active
    Spring focus;      // 0..1 focus ring
    Spring value;      // free channel: knob position, slider fraction, progress
    uint64_t lastFrame = 0;

    // Drive every channel toward the interaction, using spatial springs for
    // things that move and effect springs for things that only fade.
    void update(const Interaction& in, const ThemeTokens& t, float dt) {
        hover.step(in.hovered ? 1.0f : 0.0f, t.motion.effectsFast, dt);
        press.step(in.pressed ? 1.0f : 0.0f, t.motion.effectsFast, dt);
        focus.step(in.focused ? 1.0f : 0.0f, t.motion.effectsFast, dt);
        selected.step(in.selected ? 1.0f : 0.0f, t.motion.spatialFast, dt);
    }
};

class AnimStore {
public:
    // Advance the frame counter. Components step their own springs as they draw,
    // so this only has to age the table.
    void beginFrame(float deltaSeconds) {
        dt = deltaSeconds;
        ++frame;
        if ((frame & 0xFF) == 0) gc();
    }

    float delta() const { return dt; }
    uint64_t frameIndex() const { return frame; }

    WidgetState& get(uint64_t id) {
        WidgetState& w = table[id];
        bool firstSight = (w.lastFrame == 0);
        w.lastFrame = frame;
        // A widget appearing for the first time should not animate in from zero
        // on a state it already holds; callers can snap after this if needed.
        (void)firstSight;
        return w;
    }

    // Drop widgets that have not been drawn for a while. Menus and popups come
    // and go, so without this the table only ever grows.
    void gc(uint64_t maxAgeFrames = 600) {
        for (auto it = table.begin(); it != table.end();) {
            if (frame - it->second.lastFrame > maxAgeFrames) it = table.erase(it);
            else ++it;
        }
    }

    void clear() { table.clear(); }
    size_t size() const { return table.size(); }

private:
    std::unordered_map<uint64_t, WidgetState> table;
    uint64_t frame = 1;
    float dt = 1.0f / 60.0f;
};

// FNV-1a over a tag plus an index. Stable across frames, which is all an
// immediate-mode id has to be.
inline uint64_t widgetId(const char* tag, int index = 0) {
    uint64_t h = 1469598103934665603ull;
    for (const char* p = tag; p && *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    h ^= (uint64_t)(uint32_t)index + 0x9E3779B97F4A7C15ull;
    h *= 1099511628211ull;
    return h;
}

// Everything a component needs for one frame: which theme, where the animation
// state lives, and how long the frame was.
struct Context {
    const ThemeTokens* tokens = nullptr;
    AnimStore* anim = nullptr;
    float dt = 1.0f / 60.0f;
    float scale = 1.0f;          // UI scale, for components that size themselves

    const ThemeTokens& t() const { return *tokens; }

    // Resolve and advance one widget's animation in a single call.
    WidgetState& state(uint64_t id, const Interaction& in) {
        WidgetState& w = anim->get(id);
        w.update(in, *tokens, dt);
        return w;
    }
};

} // namespace material
