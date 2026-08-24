#pragma once

// -----------------------------------------------------------------------------
// SilverAnim - the single home for every animation in SilverGallery/SilverViewer.
//
// Nothing else should contain a hand-written `1 - exp(-k * dt)` or a magic decay
// constant: rates, epsilons, easing curves and the global on/off switch all live
// here and are driven by the "animation" block of config/silver.json.
//
//   using namespace silveranim;
//   approach(ui.scrollY, ui.targetScrollY, rates().scroll, dt);
//
// Turning "animation.enabled" off in the config makes every helper snap straight
// to the target, so the whole UI becomes instantaneous without touching any of
// the call sites.
// -----------------------------------------------------------------------------

#include "silver_config.h"
#include <cmath>
#include <algorithm>

namespace silveranim {

// -----------------------------------------------------------------------------
// Second-order dynamic spring
//
// A damped harmonic oscillator parameterised the way a designer thinks about
// motion rather than the way a solver does:
//
//   f  natural frequency (Hz)  - how fast the motion wants to happen
//   z  damping ratio           - z<1 bouncy, z=1 critically damped, z>1 sluggish
//   r  initial response        - 0 eases out of rest, >0 anticipates the target,
//                                <0 winds up before moving
//
//   y'' = 2*pi*f * ( x + (r*x')/(2*pi*f) - y ) - 2*z*(2*pi*f)*y'
//
// Solved semi-implicitly per frame, with the k2 term clamped so large frame
// times can never make it explode.
// -----------------------------------------------------------------------------
struct SpringParams {
    float k1 = 0.0f;   // damping term
    float k2 = 0.0f;   // inertia term
    float k3 = 0.0f;   // input-velocity (response) term

    void set(float frequency, float damping, float response) {
        frequency = std::max(0.01f, frequency);
        const float twoPiF = 6.2831853f * frequency;
        k1 = damping / (3.1415927f * frequency);
        k2 = 1.0f / (twoPiF * twoPiF);
        k3 = response * damping / twoPiF;
    }
};

struct SpringSpec {
    float frequency = 2.6f;
    float damping = 0.9f;
    float response = 0.0f;
    SpringParams params;

    void rebuild() { params.set(frequency, damping, response); }
};

// One animation channel: a spring plus the exponential rate used when the
// config asks for the simpler model.
struct Channel {
    float rate = 20.0f;      // exponential model
    SpringSpec spring;       // second-order model
};

// All tunable animation timings, mirrored from the config file.
struct Rates {
    bool  enabled = true;
    float speedMultiplier = 1.0f;

    float scroll = 20.0f;           // grid scroll easing
    float scrollWheelStep = 64.0f;  // pixels per wheel notch
    float layoutGlide = 26.0f;      // tiles sliding to a new grid slot
    float hover = 24.0f;            // tile hover lift / caption fade
    float select = 24.0f;           // selection ring fade
    float sidebar = 20.0f;          // FilePilot slide in/out
    float fullscreen = 20.0f;       // lightbox open/close
    float zoomPopup = 24.0f;        // zoom HUD
    float themeMenu = 24.0f;        // theme popover
    float tabIndicator = 24.0f;     // sliding tab pill
    float filmstrip = 15.0f;        // viewer filmstrip scroll
    float skeletonPulse = 4.0f;     // loading placeholder breathing

    float settleEpsilon = 0.2f;     // px below which a position snaps
    float fadeEpsilon = 0.005f;     // alpha below which a fade snaps

    bool  animateOffscreenTiles = false;
    // How far beyond the viewport tiles still take part in a layout glide.
    // Without this, a tile that scrolls in mid-transition pops into place while
    // its neighbours are still moving, so a reflow looks half-animated.
    float layoutMargin = 700.0f;
    float toastSeconds = 2.0f;
    float zoomPillSeconds = 2.0f;

    // "spring" (second-order physics) or "exponential" (plain smoothing).
    bool useSpring = true;

    Channel chScroll, chLayout, chHover, chSelect, chSidebar, chFullscreen;
    Channel chZoomPopup, chThemeMenu, chTabIndicator, chFilmstrip;

    // SilverViewer channels
    Channel chViewerTransform;   // pan / zoom / rotation
    Channel chViewerChrome;      // toolbars, scrollbar and minimap fades
    Channel chViewerPopup;       // metadata / zoom / theme popovers
    Channel chViewerGridScroll;  // grid view scrolling
};

inline Rates& rates() {
    static Rates instance;
    return instance;
}

// Pull the "animation" block out of the config. Safe to call repeatedly (the
// config hot-reloads, so this runs again whenever the file changes).
inline void reloadFromConfig() {
    const SilverConfig& c = SilverConfig::get();
    Rates& r = rates();

    r.enabled                = c.flag("animation.enabled", true);
    r.speedMultiplier        = std::max(0.05f, c.num("animation.speedMultiplier", 1.0f));
    r.scroll                 = c.num("animation.scrollRate", 20.0f);
    r.scrollWheelStep        = c.num("animation.scrollWheelStep", 64.0f);
    r.layoutGlide            = c.num("animation.layoutGlideRate", 26.0f);
    r.hover                  = c.num("animation.hoverRate", 24.0f);
    r.select                 = c.num("animation.selectRate", 24.0f);
    r.sidebar                = c.num("animation.sidebarRate", 20.0f);
    r.fullscreen             = c.num("animation.fullscreenRate", 20.0f);
    r.zoomPopup              = c.num("animation.zoomPopupRate", 24.0f);
    r.themeMenu              = c.num("animation.themeMenuRate", 24.0f);
    r.tabIndicator           = c.num("animation.tabIndicatorRate", 24.0f);
    r.filmstrip              = c.num("animation.filmstripRate", 15.0f);
    r.skeletonPulse          = c.num("animation.skeletonPulseRate", 4.0f);
    r.settleEpsilon          = c.num("animation.settleEpsilon", 0.2f);
    r.fadeEpsilon            = c.num("animation.fadeEpsilon", 0.005f);
    r.animateOffscreenTiles  = c.flag("animation.animateOffscreenTiles", false);
    r.layoutMargin           = std::max(0.0f, c.num("animation.layoutMargin", 700.0f));
    r.toastSeconds           = c.num("animation.toastSeconds", 2.0f);
    r.zoomPillSeconds        = c.num("animation.zoomPillSeconds", 2.0f);

    r.useSpring = (c.text("animation.model", "spring") != "exponential");

    auto loadChannel = [&](Channel& ch, const char* key, float rate,
                           float f, float z, float resp) {
        ch.rate = rate;
        std::string base = std::string("animation.spring.") + key + ".";
        ch.spring.frequency = c.num((base + "frequency").c_str(), f);
        ch.spring.damping   = c.num((base + "damping").c_str(), z);
        ch.spring.response  = c.num((base + "response").c_str(), resp);
        ch.spring.rebuild();
    };

    loadChannel(r.chLayout,       "tile",         r.layoutGlide,  3.0f, 1.00f,  0.0f);
    loadChannel(r.chScroll,       "scroll",       r.scroll,       2.6f, 1.05f,  0.0f);
    loadChannel(r.chSidebar,      "sidebar",      r.sidebar,      2.4f, 0.90f,  0.2f);
    loadChannel(r.chFullscreen,   "fullscreen",   r.fullscreen,   3.2f, 0.95f,  0.1f);
    loadChannel(r.chHover,        "hover",        r.hover,        4.0f, 0.85f,  0.0f);
    loadChannel(r.chSelect,       "select",       r.select,       4.0f, 0.90f,  0.0f);
    loadChannel(r.chZoomPopup,    "zoomPopup",    r.zoomPopup,    3.6f, 0.85f,  0.3f);
    loadChannel(r.chThemeMenu,    "themeMenu",    r.themeMenu,    3.6f, 0.85f,  0.3f);
    loadChannel(r.chTabIndicator, "tabIndicator", r.tabIndicator, 3.4f, 0.85f,  0.2f);
    loadChannel(r.chFilmstrip,    "filmstrip",    r.filmstrip,    2.4f, 1.00f,  0.0f);

    loadChannel(r.chViewerTransform,  "viewerTransform",  24.0f, 3.4f, 1.00f, 0.0f);
    loadChannel(r.chViewerChrome,     "viewerChrome",     14.0f, 2.6f, 1.00f, 0.0f);
    loadChannel(r.chViewerPopup,      "viewerPopup",      22.0f, 3.6f, 0.85f, 0.3f);
    loadChannel(r.chViewerGridScroll, "viewerGridScroll", 24.0f, 2.8f, 1.05f, 0.0f);
}

// -----------------------------------------------------------------------------
// Core interpolation
// -----------------------------------------------------------------------------

// Frame-rate independent smoothing factor for an exponential approach.
// Returns 1.0 (instant) when animation is switched off.
inline float blend(float rate, float dt) {
    const Rates& r = rates();
    if (!r.enabled || rate <= 0.0f || dt <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-rate * r.speedMultiplier * dt);
}

// Move `current` toward `target`; snaps once within `epsilon`.
inline void approach(float& current, float target, float rate, float dt, float epsilon) {
    if (!rates().enabled) { current = target; return; }
    current += (target - current) * blend(rate, dt);
    if (std::abs(current - target) < epsilon) current = target;
}

// Position/size flavour - uses the shared pixel epsilon.
inline void approachPos(float& current, float target, float rate, float dt) {
    approach(current, target, rate, dt, rates().settleEpsilon);
}

// Alpha/progress flavour - uses the shared fade epsilon.
inline void approachFade(float& current, float target, float rate, float dt) {
    approach(current, target, rate, dt, rates().fadeEpsilon);
}

// Snap a whole rect to its target with no easing (used for off-screen tiles).
inline void snapRect(float& x, float& y, float& w, float& h,
                     float tx, float ty, float tw, float th) {
    x = tx; y = ty; w = tw; h = th;
}

// Glide a rect toward its target using the layout rate.
inline void approachRect(float& x, float& y, float& w, float& h,
                         float tx, float ty, float tw, float th, float dt) {
    const Rates& r = rates();
    if (!r.enabled) { snapRect(x, y, w, h, tx, ty, tw, th); return; }
    float k = blend(r.layoutGlide, dt);
    x += (tx - x) * k;
    y += (ty - y) * k;
    w += (tw - w) * k;
    h += (th - h) * k;
    if (std::abs(x - tx) < r.settleEpsilon) x = tx;
    if (std::abs(y - ty) < r.settleEpsilon) y = ty;
    if (std::abs(w - tw) < r.settleEpsilon) w = tw;
    if (std::abs(h - th) < r.settleEpsilon) h = th;
}

// -----------------------------------------------------------------------------
// Second-order integration
// -----------------------------------------------------------------------------

// Advance one spring channel. `value`/`velocity` are the caller's state,
// `target` is where it should end up, `targetVelocity` lets the spring lead a
// moving target (0 for ordinary step changes).
inline void springStep(float& value, float& velocity, float target,
                       const SpringParams& p, float dt, float targetVelocity = 0.0f) {
    if (dt <= 0.0f) return;

    // Sub-step long frames so a stall (or a 30 Hz display) can never turn a
    // critically damped move into an overshoot.
    const float maxStep = 1.0f / 60.0f;
    int steps = (int)std::ceil(dt / maxStep);
    steps = std::clamp(steps, 1, 8);
    const float h = dt / (float)steps;

    // Clamp k2 for whatever step size survived, so the integrator stays stable
    // even when the sub-step cap is hit.
    const float k2Stable = std::max(p.k2, 1.1f * (h * h * 0.25f + h * p.k1 * 0.5f));

    for (int i = 0; i < steps; ++i) {
        value += velocity * h;
        velocity += h * (target + p.k3 * targetVelocity - value - p.k1 * velocity) / k2Stable;
    }
}

// Drive a value through whichever model the config selected, settling exactly
// on the target once it is within `epsilon` and moving slowly.
inline void drive(float& value, float& velocity, float target,
                  const Channel& ch, float dt, float epsilon,
                  float targetVelocity = 0.0f) {
    const Rates& r = rates();
    if (!r.enabled || dt <= 0.0f) {
        value = target;
        velocity = 0.0f;
        return;
    }

    if (r.useSpring) {
        springStep(value, velocity, target, ch.spring.params, dt * r.speedMultiplier, targetVelocity);
        if (std::abs(value - target) < epsilon && std::abs(velocity) < epsilon * 8.0f) {
            value = target;
            velocity = 0.0f;
        }
    } else {
        value += (target - value) * blend(ch.rate, dt);
        velocity = 0.0f;
        if (std::abs(value - target) < epsilon) value = target;
    }
}

// Motion state for a value whose target moves: remembers the previous target so
// the spring can measure target velocity, which is what the `response` (r)
// parameter acts on (anticipation for r>0, wind-up for r<0).
struct Motion {
    float value = 0.0f;
    float velocity = 0.0f;
    float prevTarget = 0.0f;
    bool primed = false;

    void reset(float v) {
        value = v;
        velocity = 0.0f;
        prevTarget = v;
        primed = true;
    }
};

inline void driveMotion(Motion& m, float target, const Channel& ch, float dt, float epsilon) {
    const Rates& r = rates();
    if (!m.primed) { m.reset(target); return; }

    if (!r.enabled || dt <= 0.0f) {
        m.value = target;
        m.velocity = 0.0f;
        m.prevTarget = target;
        return;
    }

    float targetVelocity = (target - m.prevTarget) / dt;
    m.prevTarget = target;

    if (r.useSpring) {
        springStep(m.value, m.velocity, target, ch.spring.params,
                   dt * r.speedMultiplier, targetVelocity);
        if (std::abs(m.value - target) < epsilon && std::abs(m.velocity) < epsilon * 8.0f) {
            m.value = target;
            m.velocity = 0.0f;
        }
    } else {
        m.value += (target - m.value) * blend(ch.rate, dt);
        m.velocity = 0.0f;
        if (std::abs(m.value - target) < epsilon) m.value = target;
    }
}

inline void driveMotionPos(Motion& m, float target, const Channel& ch, float dt) {
    driveMotion(m, target, ch, dt, rates().settleEpsilon);
}

inline void driveMotionFade(Motion& m, float target, const Channel& ch, float dt) {
    driveMotion(m, target, ch, dt, rates().fadeEpsilon);
}

// Same as driveMotion but for callers that keep the three state floats
// themselves (value, velocity, previous target) rather than a Motion struct.
inline void driveTracked(float& value, float& velocity, float& prevTarget,
                         float target, const Channel& ch, float dt, float epsilon) {
    const Rates& r = rates();
    if (!r.enabled || dt <= 0.0f) {
        value = target;
        velocity = 0.0f;
        prevTarget = target;
        return;
    }

    float targetVelocity = (target - prevTarget) / dt;
    prevTarget = target;

    if (r.useSpring) {
        springStep(value, velocity, target, ch.spring.params,
                   dt * r.speedMultiplier, targetVelocity);
        if (std::abs(value - target) < epsilon && std::abs(velocity) < epsilon * 8.0f) {
            value = target;
            velocity = 0.0f;
        }
    } else {
        value += (target - value) * blend(ch.rate, dt);
        velocity = 0.0f;
        if (std::abs(value - target) < epsilon) value = target;
    }
}

inline void driveTrackedPos(float& value, float& velocity, float& prevTarget,
                            float target, const Channel& ch, float dt) {
    driveTracked(value, velocity, prevTarget, target, ch, dt, rates().settleEpsilon);
}

inline void driveTrackedFade(float& value, float& velocity, float& prevTarget,
                             float target, const Channel& ch, float dt) {
    driveTracked(value, velocity, prevTarget, target, ch, dt, rates().fadeEpsilon);
}

inline void drivePos(float& value, float& velocity, float target, const Channel& ch, float dt) {
    drive(value, velocity, target, ch, dt, rates().settleEpsilon);
}

inline void driveFade(float& value, float& velocity, float target, const Channel& ch, float dt) {
    drive(value, velocity, target, ch, dt, rates().fadeEpsilon);
}

// Rect flavour: four independent springs sharing one channel.
struct RectVelocity {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    void clear() { x = y = w = h = 0.0f; }
};

inline void driveRect(float& x, float& y, float& w, float& h, RectVelocity& vel,
                      float tx, float ty, float tw, float th,
                      const Channel& ch, float dt) {
    drivePos(x, vel.x, tx, ch, dt);
    drivePos(y, vel.y, ty, ch, dt);
    drivePos(w, vel.w, tw, ch, dt);
    drivePos(h, vel.h, th, ch, dt);
}

// -----------------------------------------------------------------------------
// Easing curves (for one-shot, time-parameterised transitions)
// -----------------------------------------------------------------------------
inline float easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

inline float easeInOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return (t < 0.5f) ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

inline float easeOutBack(float t, float overshoot = 1.70158f) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = t - 1.0f;
    return 1.0f + (overshoot + 1.0f) * inv * inv * inv + overshoot * inv * inv;
}

// Breathing pulse for loading placeholders. Flat when animation is off.
inline float pulse(float timeSeconds, float low = 0.70f, float high = 1.0f) {
    const Rates& r = rates();
    if (!r.enabled) return high;
    float amp = (high - low) * 0.5f;
    float mid = low + amp;
    return mid + amp * std::sin(timeSeconds * r.skeletonPulse * r.speedMultiplier);
}

// Countdown helper for timers (toasts, HUD auto-close).
inline void tickTimer(float& timer, float dt) {
    if (timer <= 0.0f) return;
    timer -= dt;
    if (timer < 0.0f) timer = 0.0f;
}

// -----------------------------------------------------------------------------
// A self-contained animated scalar, for state that is only used in one place.
// -----------------------------------------------------------------------------
struct Tween {
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;
    Channel channel;

    void configure(float frequency, float damping, float response, float exponentialRate = 20.0f) {
        channel.rate = exponentialRate;
        channel.spring.frequency = frequency;
        channel.spring.damping = damping;
        channel.spring.response = response;
        channel.spring.rebuild();
    }

    void reset(float v) { value = target = v; velocity = 0.0f; }
    void to(float t) { target = t; }
    void update(float dt) { driveFade(value, velocity, target, channel, dt); }
    bool settled() const { return value == target && velocity == 0.0f; }
};

} // namespace silveranim
