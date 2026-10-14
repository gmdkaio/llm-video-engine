#pragma once
#include "render/color.h"

// Derived palette. The rules are the reference template's, so the same input
// colours give the same panel, line and text shades.
struct Theme {
    Color bg, text, accent, good, bad, warn;
    Color panel, line, grid, text2, text3, accentDim, neutral, neutral2, goodText, blue;
    bool dark = true;

    // panel: pass a.a == 0 to derive it from bg.
    static Theme make(Color bg, Color text, Color accent, Color good, Color bad, Color warn,
                      Color panel = {}, Color blue = Color::hex("#4E9CF5")) {
        Theme t{bg, text, accent, good, bad, warn};
        const Color white{255, 255, 255, 1}, black{0, 0, 0, 1};
        t.dark = luminance(bg) < 0.5f;
        t.panel = panel.a > 0 ? panel : t.dark ? mix(bg, white, .05f) : (luminance(bg) > .97f ? mix(bg, text, .035f) : white);
        t.line = mix(bg, text, .12f);
        t.grid = mix(bg, text, .08f);
        t.text2 = mix(text, bg, .3f);
        t.text3 = mix(text, bg, .5f);
        t.accentDim = mix(accent, t.panel, .55f);
        t.neutral = mix(text, t.panel, .45f);
        t.neutral2 = mix(text, t.panel, .62f);
        t.goodText = t.dark ? mix(good, white, .25f) : mix(good, black, .15f);
        t.blue = blue;
        return t;
    }
};
