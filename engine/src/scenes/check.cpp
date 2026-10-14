// Layout checks for `llm-video-engine validate`: where every visible text ends up at the end
// of each scene, in stage coordinates, then text off the stage and text over other text.
#include <algorithm>
#include <cmath>
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

void collect(const Node& n, float t, const Xf& parent, float alpha, const Fonts& fonts, std::vector<Box>& out) {
    alpha *= val(n.opacity, t, 1);
    if (alpha < .05f) return;
    const float s = val(n.scale, t, 1);
    const float sx = s * val(n.scaleX, t, 1), sy = s * val(n.scaleY, t, 1);
    if (sx == 0 || sy == 0) return;
    if (n.clip && val(n.clipWidth, t, n.clipW) <= 0) return;
    const Xf xf = parent.then(n.x + val(n.tx, t, 0), n.y + val(n.ty, t, 0), n.ox, n.oy, sx, sy);
    if (n.kind == NodeKind::Text) {
        std::string str = n.format ? n.format(val(n.value, t, 0)) : n.text;
        const float w = n.blob && !n.format ? n.blobWidth : shape(fonts, str, n.style).width;
        if (w > 0) {
            const float off = n.style.align == Align::Center ? -w / 2 : n.style.align == Align::Right ? -w : 0;
            // Ink box: cap height above the baseline, and descender space only when a letter needs it.
            const bool desc = str.find_first_of("gjpqy,;()[]") != std::string::npos || str.find("\xc3\xa7") != std::string::npos;
            out.push_back({xf.dx + xf.sx * off, xf.dy - xf.sy * n.style.size * .72f, xf.dx + xf.sx * (off + w),
                           xf.dy + xf.sy * n.style.size * (desc ? .22f : .02f), str});
        }
    }
    for (const auto& ch : n.children) collect(*ch, t, xf, alpha, fonts, out);
}

std::string clip(const std::string& s) { return s.size() > 40 ? s.substr(0, 37) + "..." : s; }

}  // namespace

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
