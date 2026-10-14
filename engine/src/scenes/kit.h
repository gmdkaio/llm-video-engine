#pragma once
// Shared context for scene and component builders: theme, fonts, locale, time
// resolution, text helpers and the GSAP-equivalent animation helpers of the
// reference template (pop, fade, grow, counter).
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/at.h"
#include "core/format.h"
#include "render/graph.h"
#include "render/text.h"
#include "render/theme.h"

using nlohmann::json;

// Montserrat metrics: ascent .968 em, normal line height 1.219 em.
constexpr float kAscent = 0.968f, kNormalLine = 1.219f;

inline float baseline(float top, float size, float lineHeight) {
    return top + (lineHeight - size * kNormalLine) / 2 + size * kAscent;
}

namespace ease {
inline const Ease p1Out = Ease::parse("power1.out"), p2Out = Ease::parse("power2.out"), p2In = Ease::parse("power2.in"),
                  p2InOut = Ease::parse("power2.inOut"), p3Out = Ease::parse("power3.out"), p3InOut = Ease::parse("power3.inOut"),
                  pop = Ease::parse("back.out(1.6)"), linear = Ease::parse("none");
inline Ease back(float k) { Ease e = Ease::parse("back.out"); e.back = k; return e; }
}  // namespace ease

struct Rect { float x = 0, y = 0, w = 0, h = 0; };

// What annotations need to know about a chart drawn earlier in the same box.
struct ChartInfo {
    Node* layer = nullptr;                 // annotations are added here, in the chart's coordinates
    float left = 0, right = 0, top = 0, bottom = 0;  // plot area
    float slot = 0;                        // x distance between categories
    std::function<float(int)> x;           // centre of category i
    std::function<float(double)> y;        // value -> y
    std::vector<double> values;            // bar values (bars) or the first series (lines)
    std::vector<double> tops;              // value at the top of each bar (with stack)
    std::vector<Node*> bars;               // bar rects, for recolor and pulse
    std::vector<Node*> labels;             // bar value labels
    std::vector<bool> projection;
    std::map<std::string, std::vector<double>> series;
    std::map<std::string, Node*> seriesPaths;
};

struct Kit {
    const Fonts& fonts;
    Theme th;
    Locale loc;
    float pixelScale = 1.5f;              // output pixels per stage unit
    std::vector<float> B;                  // scene boundaries
    std::vector<std::string> warnings;
    // Current scene.
    int k = 0;
    float S0 = 0, t0 = 0;                  // scene start, speech start
    at::Resolver when;
    std::map<std::string, ChartInfo> charts;

    float at(const json& spec, float dflt) const { return when(spec, dflt); }

    // Colors by tone name.
    Color tone(const std::string& t) const {
        if (t == "good") return th.good;
        if (t == "bad") return th.bad;
        if (t == "warn") return th.warn;
        if (t == "neutral") return th.neutral;
        if (t == "blue") return th.blue;
        if (t == "text") return th.text;
        if (t == "text2") return th.text2;
        if (t == "text3") return th.text3;
        return th.accent;
    }
    // Readable text on a filled tone.
    Color onTone(const std::string& t) const {
        const Color c = tone(t);
        return luminance(c) > 0.55f ? mix(c, Color{0, 0, 0, 1}, .78f) : Color{255, 255, 255, 1};
    }

    float measure(const std::string& s, const TextStyle& st) const { return ::measure(fonts, s, st); }
    std::vector<std::string> wrap(const std::string& s, const TextStyle& st, float w) const { return ::wrap(fonts, s, st, w); }
};

inline TextStyle style(float size, int weight, Align a = Align::Left, float letterSpacingEm = 0) {
    TextStyle st;
    st.size = size; st.weight = weight; st.align = a; st.letterSpacing = letterSpacingEm * size;
    return st;
}

inline std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    // Common accented lowercase letters in UTF-8 (U+00E0..U+00FE): clear the case bit of the second byte.
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if ((unsigned char)s[i] == 0xC3 && (unsigned char)s[i + 1] >= 0xA0 && (unsigned char)s[i + 1] <= 0xBE && (unsigned char)s[i + 1] != 0xB7)
            s[i + 1] = (char)((unsigned char)s[i + 1] - 0x20);
    return s;
}

// Animations, as the reference template's helpers.
inline void fade(Node* n, float t, float d = .3f) { n->opacity.fromTo(0, 1, t, d, ease::p1Out); }
inline void pop(Node* n, float t, float dy = 14, float s = .96f) {
    n->opacity.fromTo(0, 1, t, .5f, ease::pop);
    n->ty.fromTo(dy, 0, t, .5f, ease::pop);
    if (s != 1) n->scale.fromTo(s, 1, t, .5f, ease::pop);
}
// Slide in: opacity 0->1 while moving (dx, dy) -> 0.
inline void enter(Node* n, float t, float dx, float dy, float d = .4f, Ease e = ease::p1Out) {
    n->opacity.fromTo(0, 1, t, d, e);
    if (dx != 0) n->tx.fromTo(dx, 0, t, d, e);
    if (dy != 0) n->ty.fromTo(dy, 0, t, d, e);
}
// scaleY 0 -> 1 from the bottom (or the top for downward bars).
inline void grow(Node* r, float t, bool fromTop = false, float d = .6f) {
    r->ox = r->w / 2;
    r->oy = fromTop ? 0 : r->h;
    r->scaleY.fromTo(0, 1, t, d, ease::p3Out);
}
inline void growX(Node* r, float t, float d, Ease e) {
    r->ox = 0;
    r->oy = r->h / 2;
    r->scaleX.fromTo(0, 1, t, d, e);
}
// Brief color flashes: to `hi` and back, `times` times of `d` seconds each way.
inline void pulse(Node* n, float t, Color hi, int times = 2, float d = .22f) {
    const Color c0 = n->color.empty() ? n->fill : n->color.eval(t);
    for (int i = 0; i < times; ++i) {
        n->color.to(hi, t + 2 * i * d, d, ease::p1Out, c0);
        n->color.to(c0, t + (2 * i + 1) * d, d, ease::p1Out, c0);
    }
}

// Text node whose top-left box is (x, top), with a single line of the given style.
inline Node* label(Node* parent, float x, float top, const std::string& s, const TextStyle& st, Color c, float lineHeight = 0) {
    return parent->add(text(x, baseline(top, st.size, lineHeight > 0 ? lineHeight : st.size * kNormalLine), s, st, c));
}

// Counter: hidden until it starts, then fades/slides in while counting from `from` to `to`.
inline Node* counter(Node* parent, float x, float baselineY, double from, double to, float t, float dur,
                     std::function<std::string(double)> fmt, const TextStyle& st, Color c) {
    Node* n = parent->add(text(x, baselineY, "", st, c));
    n->format = std::move(fmt);
    n->opacity.set(0, 0);
    n->opacity.fromTo(0, 1, t, .35f, ease::p1Out);
    n->ty.fromTo(10, 0, t, .35f, ease::p1Out);
    n->value.fromTo((float)from, (float)to, t, dur, ease::p2Out);
    return n;
}

// Component builders. Each draws into `r` (in the parent's coordinates) under `parent`.
void buildComponent(Kit& kit, Node* parent, const json& c, Rect r);
// Natural height of a text-like component at width w; < 0 when it should grow instead.
float intrinsicHeight(const Kit& kit, const json& c, float w);
bool isAnnotation(const std::string& type);
void buildAnnotation(Kit& kit, const json& c);
// A chip (pill with tone colors); returns its group, positioned at (x, y). Width in *w.
Node* chip(Kit& kit, Node* parent, float x, float y, const std::string& textStr, const std::string& tone, float font = 12.5f,
           float padX = 11, float padY = 6, float* w = nullptr);
