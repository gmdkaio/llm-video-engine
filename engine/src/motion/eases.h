#pragma once
// The 12 measured eases of motionmaxxing (github.com/Tejashmakwana/motionmaxxing, runtime/motion.js,
// Apache-2.0, Tejas Makwana): curves fitted to frame-sampled moves in human-made films. Every f(0) = 0
// and f(1) = 1; some pass 1 on the way (overshoot). Typical durations are in frames at 30 fps.
#include <cmath>
#include <stdexcept>
#include <string>

#include "core/easing.h"

namespace motion {

namespace curve {
inline double powerOut(double p, double t) { return 1 - std::pow(1 - t, p); }
inline double powerIn(double p, double t) { return std::pow(t, p); }
inline double expoOut(double k, double t) { return (1 - std::exp(-k * t)) / (1 - std::exp(-k)); }
// Most of the travel at once, then a linear tail.
inline double snapRamp(double w, double tau, double t) {
    return (w * (1 - std::exp(-t / tau)) + (1 - w) * t) / (w * (1 - std::exp(-1 / tau)) + (1 - w));
}
// Hits the mark at once and passes it.
inline double snapOver(double A, double ta, double td, double t) {
    auto f = [&](double x) { return (1 + A * std::exp(-x / td)) * (1 - std::exp(-x / ta)); };
    return f(t) / f(1);
}
// Damped spring.
inline double spring(double c, double om, double t) {
    auto f = [&](double x) { return 1 - std::exp(-c * x) * (std::cos(om * x) + (c / om) * std::sin(om * x)); };
    return f(t) / f(1);
}
inline double smooth(double p, double t) {
    const double a = std::pow(t, p), b = std::pow(1 - t, p);
    return a / (a + b);
}
}  // namespace curve

#define LVE_EASE(name, expr) \
    inline float name(float t) { return t <= 0 ? 0.f : t >= 1 ? 1.f : (float)(expr); }
LVE_EASE(crashIn, curve::snapOver(2.0, 0.19, 0.218, t))   // passes its mark at once: 1.18 at t ~.27
LVE_EASE(popOver, curve::spring(2.67, 4.36, t))           // slow late lean: 1.08 at t ~.72
LVE_EASE(bounceHard, curve::spring(1.34, 5.07, t))        // big overshoot 1.48; rare
LVE_EASE(snapSettle, curve::snapRamp(0.942, 0.118, t))    // hero entry: 54% of the travel in the first 10% of the time
LVE_EASE(softLand, curve::expoOut(3.41, t))               // firm decelerate: text and panels landing
LVE_EASE(whip, curve::powerOut(2.52, t))                  // hard decelerating big move
LVE_EASE(glide, curve::powerOut(1.7, t))                  // gentle ease-out, medium moves
LVE_EASE(cruise, t)                                       // linear: fades, drifts
LVE_EASE(softInOut, curve::smooth(1.42, t))               // symmetric S: opacity, blur
LVE_EASE(gentleIn, curve::powerIn(2.07, t))               // mild acceleration
LVE_EASE(accelExit, curve::powerIn(2.6, t))               // the leaving curve
LVE_EASE(crashOut, curve::powerIn(3.97, t))               // hard acceleration into a cut
LVE_EASE(smearDecay, curve::powerOut(2.5, t))             // a smear's blur: (1 - t)^2.5 of its peak
#undef LVE_EASE

inline Ease fn(float (*f)(float)) {
    Ease e;
    e.kind = EaseKind::Fn;
    e.fn = f;
    return e;
}

struct Named { const char* name; float (*f)(float); int frames; };
inline constexpr Named kEases[] = {
    {"crashIn", crashIn, 10},   {"popOver", popOver, 15},     {"bounceHard", bounceHard, 22}, {"snapSettle", snapSettle, 11},
    {"softLand", softLand, 12}, {"whip", whip, 7},            {"glide", glide, 10},           {"cruise", cruise, 9},
    {"softInOut", softInOut, 9}, {"gentleIn", gentleIn, 9},   {"accelExit", accelExit, 13},   {"crashOut", crashOut, 17},
};

// A measured ease by name, else a GSAP name ("power2.out").
inline Ease parse(const std::string& s) {
    for (const auto& e : kEases)
        if (s == e.name) return fn(e.f);
    return Ease::parse(s);
}
// The ease's typical duration in frames at 30 fps (12 when unknown).
inline int frames(const std::string& s) {
    for (const auto& e : kEases)
        if (s == e.name) return e.frames;
    return 12;
}

}  // namespace motion
