#pragma once
#include <algorithm>
#include <vector>

#include "core/easing.h"

// One tween on one property. Value is a pure function of t.
template <class V>
struct Tween {
    V from{}, to{};
    float at = 0, dur = 0;
    Ease ease;
};

template <class V>
inline V lerp(const V& a, const V& b, float k) { return a + (b - a) * k; }

// Tweens on one property, sorted by start time. Before the first tween the
// property holds the first tween's `from` (GSAP fromTo renders immediately);
// after that, the latest started tween decides the value.
template <class V>
struct Track {
    std::vector<Tween<V>> tweens;

    bool empty() const { return tweens.empty(); }

    V eval(float t) const {
        const Tween<V>* cur = &tweens.front();
        for (const auto& tw : tweens) {
            if (tw.at <= t) cur = &tw; else break;
        }
        if (t < cur->at) return cur->from;
        const float k = cur->dur > 0 ? std::clamp((t - cur->at) / cur->dur, 0.f, 1.f) : 1.f;
        return lerp(cur->from, cur->to, cur->ease(k));
    }

    void add(Tween<V> tw) {
        tweens.push_back(tw);
        std::stable_sort(tweens.begin(), tweens.end(),
                         [](const Tween<V>& a, const Tween<V>& b) { return a.at < b.at; });
    }
    void fromTo(V from, V to, float at, float dur, Ease ease = {}) { add({from, to, at, dur, ease}); }
    void set(V v, float at) { add({v, v, at, 0, {}}); }

    // A `to` tween starts from whatever value the property has when it begins.
    void to(V to, float at, float dur, Ease ease, V initial) {
        const V from = tweens.empty() ? initial : eval(at);
        add({from, to, at, dur, ease});
    }
};
