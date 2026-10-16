#pragma once
// Motion grammar after motionmaxxing (github.com/Tejashmakwana/motionmaxxing, Apache-2.0): moves sized
// in frames from measured human-made films. Elements arrive by moving, with opacity as a short helper;
// landings decelerate, exits accelerate; fast moves smear along their axis for a few frames; numbers
// count in steps that shrink; whole words build one by one at a speech-like pace.
// Everything writes ordinary tracks, so every frame stays a pure function of t.
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/timeline.h"
#include "motion/eases.h"
#include "render/graph.h"

namespace motion {

// Which grammar the scene builders use. Set once per plan (config.motion) before building.
enum class Style { Classic, Measured };
inline Style style = Style::Classic;
inline bool measured() { return style == Style::Measured; }

inline float F(float frames) { return frames / timeline::kFps; }
// Steps land a hair before their frame time, so sampling exactly on the frame sees them.
constexpr float kStepEps = 1e-4f;

// Directional blur along one axis: peak (length px, sigma px/2) on the first frame, gone after `frames`.
// With `peakLast` it grows into the last frame instead (an exit into a cut).
inline void smear(Node* n, float at, bool vertical, float px, float frames = 3, bool peakLast = false) {
    Track<float>& tr = vertical ? n->blurY : n->blurX;
    if (tr.empty()) tr.set(0, 0);
    if (peakLast) tr.fromTo(0, px / 2, at, F(frames), fn(gentleIn));
    else tr.fromTo(px / 2, 0, at, F(frames), fn(smearDecay));
}

struct Pose { float dx = 0, dy = 0, scale = 1, blur = 0; };

// Arrival: hidden before `at`, then travels from the pose to rest. Opacity is a short linear helper.
inline void arrive(Node* n, float at, Pose from, float frames = 12, Ease e = fn(softLand), float fadeFrames = 5, bool withSmear = true) {
    n->opacity.fromTo(0, 1, at, F(std::min(frames, fadeFrames)), fn(cruise));
    if (from.dx != 0) n->tx.fromTo(from.dx, 0, at, F(frames), e);
    if (from.dy != 0) n->ty.fromTo(from.dy, 0, at, F(frames), e);
    if (from.scale != 1) n->scale.fromTo(from.scale, 1, at, F(frames), e);
    if (from.blur > 0) {
        n->blurX.fromTo(from.blur, 0, at, F(frames), fn(softInOut));
        n->blurY.fromTo(from.blur, 0, at, F(frames), fn(softInOut));
    }
    const float d = std::max(std::fabs(from.dx), std::fabs(from.dy));
    if (withSmear && d >= 30) smear(n, at, std::fabs(from.dy) > std::fabs(from.dx), std::max(6.f, d * .25f));
}

// Departure: accelerates to the pose and fades over its last frames, ending at full speed.
inline void depart(Node* n, float at, Pose to, float frames = 13, float fadeFrames = 5) {
    const Ease e = fn(accelExit);
    if (to.dx != 0) n->tx.to(to.dx, at, F(frames), e, 0);
    if (to.dy != 0) n->ty.to(to.dy, at, F(frames), e, 0);
    if (to.scale != 1) n->scale.to(to.scale, at, F(frames), e, 1);
    const float fd = std::min(frames, fadeFrames);
    n->opacity.to(0, at + F(frames - fd), F(fd), fn(cruise), 1);
}

// Whole-word build: each word arrives whole on its onset. Gaps are speech-like: a beat after the
// first word, quick middle, a slower step into the last word. Returns the onsets.
enum class Mech { Rise, Slide, Pop, Shrink, Focus };
inline std::vector<float> words(const std::vector<Node*>& ws, float at, Mech mech = Mech::Rise, float dist = 20) {
    std::vector<float> on;
    float f = 0;
    const int n = (int)ws.size();
    for (int i = 0; i < n; ++i) {
        on.push_back(at + F(f));
        f += i == 0 ? 4 : i == n - 2 ? 6 : 3;
    }
    for (int i = 0; i < n; ++i) {
        Pose p;
        Ease e = fn(softLand);
        float frames = 12;
        switch (mech) {
        case Mech::Rise: p.dy = dist; break;
        case Mech::Slide: p.dx = dist * 2.5f; break;
        case Mech::Pop: p.scale = .5f; e = fn(popOver); frames = 15; break;
        case Mech::Shrink: p.scale = 1.6f; break;
        case Mech::Focus: p.blur = dist * .4f; e = fn(softInOut); frames = 9; break;
        }
        arrive(ws[i], on[i], p, frames, e, 5, false);
        if (mech == Mech::Rise || mech == Mech::Slide) smear(ws[i], on[i], mech == Mech::Rise, std::max(6.f, dist * .3f));
    }
    return on;
}

// Counter: one value per frame from `from` to `to`, step sizes shrinking, landing exactly on `to`.
// Digits are never eased between frames.
inline void count(Track<float>& value, double from, double to, float at, int frames) {
    const int N = std::max(1, frames);
    std::vector<double> w(N);
    double tot = 0;
    for (int i = 0; i < N; ++i) tot += w[i] = std::pow(1 - (double)i / N, 1.6) + .12;
    value.set((float)from, 0);
    double acc = 0;
    for (int i = 0; i < N; ++i) {
        acc += w[i];
        const double v = i == N - 1 ? to : from + (to - from) * acc / tot;
        value.set((float)v, at + F((float)i + 1) - kStepEps);
    }
}

// One-frame snap: most of the way (`share`) in a single frame, then a decelerating tail.
// Starts from the track's value at `at` (or `initial` when the track is empty).
inline void snapTo(Track<float>& tr, float to, float at, float initial, float share = .6f, float tail = 15) {
    const float a = tr.empty() ? initial : tr.eval(at), mid = a + (to - a) * share;
    tr.fromTo(a, mid, at, F(1), fn(cruise));
    tr.fromTo(mid, to, at + F(1), F(tail), fn(softLand));
}

// Whip, departing half: accelerates sideways by dx and ends on the cut at peak speed, smear growing
// into the last frames. The node is hidden from the cut on.
inline void whipOut(Node* n, float cut, float dx, float frames = 7) {
    const float at = cut - F(frames);
    n->tx.to(dx, at, F(frames), fn(accelExit), 0);
    smear(n, cut - F(4), false, std::min(140.f, std::fabs(dx) * .15f), 4, true);
    n->opacity.set(0, cut - kStepEps);
}
// Whip, entry half: cuts on already moving along the same vector (from dx to rest), smear peaking on frame 1.
inline void whipIn(Node* n, float at, float dx, float frames = 8) {
    n->opacity.set(1, at - kStepEps);
    n->tx.fromTo(dx, 0, at, F(frames), fn(whip));
    smear(n, at, false, std::min(90.f, std::max(8.f, std::fabs(dx) * .15f)), 4);
}

// Constant-octave dive: the scale multiplies by `perStep` every frame and never slows down.
inline void dive(Node* n, float at, int frames = 9, float perStep = 1.6f) {
    for (int i = 0; i < frames; ++i)
        n->scale.fromTo(std::pow(perStep, (float)i), std::pow(perStep, (float)i + 1), at + F((float)i), F(1), fn(cruise));
}

// Linear creep so a held frame is never dead.
inline void drift(Track<float>& tr, float from, float to, float t0, float t1) { tr.fromTo(from, to, t0, t1 - t0, fn(cruise)); }

}  // namespace motion
