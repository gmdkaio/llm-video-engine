#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// Straight-alpha colour, channels 0..255 for rgb and 0..1 for alpha, as in CSS.
struct Color {
    float r = 0, g = 0, b = 0, a = 0;

    Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
    Color operator-(const Color& o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
    Color operator*(float k) const { return {r * k, g * k, b * k, a * k}; }

    Color alpha(float na) const { return {r, g, b, na}; }

    uint32_t argb(float opacity = 1) const {
        auto c = [](float v) { return (uint32_t)std::lround(std::fmax(0, std::fmin(255, v))); };
        return (c(a * opacity * 255) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
    }

    static Color hex(const std::string& s) {
        unsigned v = 0;
        std::string h = s[0] == '#' ? s.substr(1) : s;
        if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
        std::sscanf(h.c_str(), "%x", &v);
        return {(float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255), 1};
    }
};

// Same mixing as the reference template: channel-wise, then rounded.
inline Color mix(const Color& a, const Color& b, float t) {
    return {std::round(a.r + (b.r - a.r) * t), std::round(a.g + (b.g - a.g) * t),
            std::round(a.b + (b.b - a.b) * t), 1};
}

inline float luminance(const Color& c) { return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255; }
