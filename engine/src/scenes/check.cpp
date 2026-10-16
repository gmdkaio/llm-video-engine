// Layout checks for `llm-video-engine validate`: where every visible text ends up at the end
// of each scene, in stage coordinates, then text off the stage and text over other text.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render/text.h"
#include "scenes/build.h"
#include "scenes/kit.h"

namespace {

// Translate + scale only; rotation is ignored (no text is rotated).
struct Xf {
    float sx = 1, sy = 1, dx = 0, dy = 0;
    Xf then(float tx, float ty, float ox, float oy, float s1, float s2) const {
        // parent ∘ T(tx, ty) ∘ T(o) ∘ S(s) ∘ T(-o)
        Xf r;
        r.sx = sx * s1;
        r.sy = sy * s2;
        r.dx = dx + sx * (tx + ox - ox * s1);
        r.dy = dy + sy * (ty + oy - oy * s2);
        return r;
    }
};

struct Box {
    float l, t, r, b;
    std::string text;
};

float val(const Track<float>& tr, float t, float d) { return tr.empty() ? d : tr.eval(t); }

// `clipR` is the active clip rectangle in stage coordinates (a camera zoom crops what leaves it on purpose,
// so text cut by it is not a layout error).
void collect(const Node& n, float t, const Xf& parent, float alpha, const Fonts& fonts, std::vector<Box>& out,
             Box clipR = {-1e9f, -1e9f, 1e9f, 1e9f, ""}) {
    alpha *= val(n.opacity, t, 1);
    if (alpha < .05f) return;
    const float s = val(n.scale, t, 1);
    const float sx = s * val(n.scaleX, t, 1), sy = s * val(n.scaleY, t, 1);
    if (sx == 0 || sy == 0) return;
    if (n.clip && val(n.clipWidth, t, n.clipW) <= 0) return;
    const Xf xf = parent.then(n.x + val(n.tx, t, 0), n.y + val(n.ty, t, 0), n.ox, n.oy, sx, sy);
    if (n.clip) {
        const float cw = val(n.clipWidth, t, n.clipW);
        clipR.l = std::max(clipR.l, xf.dx + xf.sx * n.clipX);
        clipR.t = std::max(clipR.t, xf.dy + xf.sy * n.clipY);
        clipR.r = std::min(clipR.r, xf.dx + xf.sx * (n.clipX + cw));
        clipR.b = std::min(clipR.b, xf.dy + xf.sy * (n.clipY + n.clipH));
    }
    if (n.kind == NodeKind::Text) {
        std::string str = n.format ? n.format(val(n.value, t, 0)) : n.text;
        const float w = n.blob && !n.format ? n.blobWidth : shape(fonts, str, n.style).width;
        if (w > 0) {
            const float off = n.style.align == Align::Center ? -w / 2 : n.style.align == Align::Right ? -w : 0;
            // Ink box: cap height above the baseline, and descender space only when a letter needs it.
            const bool desc = str.find_first_of("gjpqy,;()[]") != std::string::npos || str.find("\xc3\xa7") != std::string::npos;
            const Box b{xf.dx + xf.sx * off, xf.dy - xf.sy * n.style.size * .72f, xf.dx + xf.sx * (off + w),
                        xf.dy + xf.sy * n.style.size * (desc ? .22f : .02f), str};
            if (b.l >= clipR.l - 1 && b.r <= clipR.r + 1 && b.t >= clipR.t - 1 && b.b <= clipR.b + 1) out.push_back(b);
        }
    }
    for (const auto& ch : n.children) collect(*ch, t, xf, alpha, fonts, out, clipR);
}

std::string clip(const std::string& s) { return s.size() > 40 ? s.substr(0, 37) + "..." : s; }

}  // namespace

// Every animated value of the visible part of a subtree at t, in a fixed order (what changes between two
// samples is motion; a hidden subtree contributes only its opacity).
void sample(const Node& n, float t, std::vector<float>& out) {
    const float a = val(n.opacity, t, 1);
    out.push_back(a);
    if (a < .001f) return;
    for (const Track<float>* tr : {&n.tx, &n.ty, &n.scale, &n.scaleX, &n.scaleY, &n.rotation, &n.value, &n.clipWidth, &n.trim, &n.blurX, &n.blurY})
        if (!tr->empty()) out.push_back(tr->eval(t));
    for (const Track<Color>* tr : {&n.color, &n.strokeColor})
        if (!tr->empty()) { const Color c = tr->eval(t); out.insert(out.end(), {c.r, c.g, c.b, c.a}); }
    for (const auto& ch : n.children) sample(*ch, t, out);
}

std::vector<std::string> checkStill(const Video& v, float minRun) {
    std::vector<std::string> out;
    const float dt = .1f;
    for (size_t k = 0; k < v.scenes.size(); ++k) {
        // Intro and outro are brand holds: only content and slide scenes must keep moving.
        if (v.types[k] == "intro" || v.types[k] == "outro") continue;
        const float s0 = v.B[k], s1 = v.B[k + 1];
        std::vector<float> prev, cur;
        float runStart = -1;
        auto flush = [&](float end) {
            if (runStart < 0 || end - runStart < minRun) return;
            char buf[160];
            std::snprintf(buf, sizeof buf, "scene %zu \xc2\xb7 STILL \xc2\xb7 %.1f s without motion, from %.1f s into the scene", k + 1, end - runStart, runStart - s0);
            out.push_back(buf);
        };
        for (float t = s0; t <= s1; t += dt) {
            cur.clear();
            sample(*v.scenes[k], t, cur);
            bool same = !prev.empty() && cur.size() == prev.size();
            for (size_t i = 0; same && i < cur.size(); ++i) same = std::fabs(cur[i] - prev[i]) < 1e-4f;
            if (same) { if (runStart < 0) runStart = t - dt; }
            else { flush(t - dt); runStart = -1; }
            std::swap(prev, cur);
        }
        flush(s1);
    }
    return out;
}

std::vector<std::string> checkLayout(const Video& v, const Fonts& fonts) {
    std::vector<std::string> out;
    for (size_t k = 0; k < v.scenes.size(); ++k) {
        const float t = v.B[k + 1] - .6f;
        std::vector<Box> boxes;
        collect(*v.scenes[k], t, Xf{}, 1, fonts, boxes);
        const std::string where = "scene " + std::to_string(k + 1) + " \xc2\xb7 ";
        for (const auto& b : boxes) {
            if (b.l < -1 || b.r > 1281 || b.t < -1 || b.b > 721)
                out.push_back(where + "TEXT_OUT \xc2\xb7 '" + clip(b.text) + "' runs off the frame");
        }
        // Overlaps: more than a sliver of shared area between two texts.
        for (size_t i = 0; i < boxes.size(); ++i)
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                const Box &a = boxes[i], &c = boxes[j];
                const float w = std::min(a.r, c.r) - std::max(a.l, c.l), h = std::min(a.b, c.b) - std::max(a.t, c.t);
                if (w > 2 && h > 3)
                    out.push_back(where + "OVERLAP \xc2\xb7 '" + clip(a.text) + "' overlaps '" + clip(c.text) + "'");
            }
    }
    return out;
}
