// The page frame and the structural scenes: background, header (logos, tabs,
// period), progress bar, intro, outro, the content scene's title block and the
// body layout that places components. Layout numbers mirror the reference
// template's CSS.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "render/image.h"
#include "scenes/build.h"
#include "scenes/kit.h"

namespace {

constexpr float kW = 1280, kH = 720;
constexpr float kInnerX = 28, kInnerY = 72, kBodyTop = 86, kBodyW = 1224, kBodyH = 534;

Color hexOr(const json& o, const char* key, const char* dflt) {
    return Color::hex(o.contains(key) ? o[key].get<std::string>() : std::string(dflt));
}

struct Logos {
    LoadedImage brand, mark, client;
    float clientScale = 1;
    float brandReveal = 0;
};

// ---------- body layout ----------

bool isBox(const json& n) { return !n.contains("type"); }

void layoutBox(Kit& kit, Node* parent, const json& box, Rect r) {
    const Theme& th = kit.th;
    Node* g = parent->add(group(r.x, r.y));
    if (box.contains("at")) {
        g->ox = r.w / 2; g->oy = r.h / 2;
        pop(g, kit.at(box["at"], .2f), 16, 1);
    }
    Rect in{0, 0, r.w, r.h};
    if (box.contains("panel") && !(box["panel"].is_boolean() && !box["panel"].get<bool>())) {
        g->add(rect(0, 0, r.w, r.h, th.panel, 12));
        in = {18, 16, r.w - 36, r.h - 32};
        const json& p = box["panel"];
        if (p.is_object() && p.contains("title")) {
            const TextStyle ts = style(12, 600), ss = style(10.5f, 500);
            const float lh = 12 * kNormalLine, b = baseline(16, 12, lh);
            const std::string title = p["title"].get<std::string>();
            g->add(text(18, b, title, ts, th.text));
            if (p.contains("sub"))
                g->add(text(18 + kit.measure(title, ts) + 8, b, p["sub"].get<std::string>(), ss, th.text3));
            in.y += lh + 8;
            in.h -= lh + 8;
        }
    }

    const bool row = box.value("dir", "column") == "row";
    const float gap = box.value("gap", 16.f);
    std::vector<const json*> flow, floats;
    for (const auto& it : box["items"]) {
        if (!isBox(it) && it.contains("float")) floats.push_back(&it);
        else flow.push_back(&it);
    }

    // Sizes along the main axis: fixed, natural (text-like in a column), or a share of the rest.
    const float total = row ? in.w : in.h, cross = row ? in.h : in.w;
    std::vector<float> size(flow.size(), -1), grow(flow.size(), 0);
    float fixed = gap * std::max(0, (int)flow.size() - 1), growSum = 0;
    for (size_t i = 0; i < flow.size(); ++i) {
        const json& it = *flow[i];
        if (it.contains("size")) size[i] = it["size"].get<float>();
        else if (!row && !isBox(it)) size[i] = intrinsicHeight(kit, it, cross);
        if (size[i] >= 0) fixed += size[i];
        else { grow[i] = it.value("grow", 1.f); growSum += grow[i]; }
    }
    const float rest = std::max(0.f, total - fixed);
    float pos = row ? in.x : in.y;
    for (size_t i = 0; i < flow.size(); ++i) {
        const float s = size[i] >= 0 ? size[i] : (growSum > 0 ? rest * grow[i] / growSum : 0);
        const Rect cr = row ? Rect{pos, in.y, s, in.h} : Rect{in.x, pos, in.w, s};
        if (isBox(*flow[i])) layoutBox(kit, g, *flow[i], cr);
        else buildComponent(kit, g, *flow[i], cr);
        pos += s + gap;
    }

    for (const json* f : floats) {
        const json& fl = (*f)["float"];
        const float w = fl.value("w", in.w / 3), x = fl.value("x", 0.f), y = fl.value("y", 0.f);
        float h = intrinsicHeight(kit, *f, w);
        if (h < 0) h = in.h / 2;
        const std::string a = fl.value("anchor", "top-left");
        const float fx = a.find("right") != std::string::npos ? in.x + in.w - w - x : in.x + x;
        const float fy = a.find("bottom") != std::string::npos ? in.y + in.h - h - y : in.y + y;
        buildComponent(kit, g, *f, {fx, fy, w, h});
    }
}

// ---------- scenes ----------

void titleBlock(Kit& kit, Node* in, const json& c) {
    const Theme& th = kit.th;
    if (c.contains("kicker") && !c["kicker"].empty()) {
        const auto& kk = c["kicker"];
        const TextStyle bold = style(10.5f, 700, Align::Left, .12f), rest = style(10.5f, 600, Align::Left, .12f);
        const float b = baseline(0, 10.5f, 10.5f * kNormalLine);
        float x = 0;
        if (kk.size() > 1) {
            const std::string s = upper(kk[0].get<std::string>());
            in->add(text(0, b, s, bold, th.accent));
            x = kit.measure(s, bold);
            in->add(text(x, b, " \xc2\xb7 " + upper(kk[1].get<std::string>()), rest, th.text3));
        } else {
            in->add(text(0, b, upper(kk[0].get<std::string>()), rest, th.text3));
        }
    }
    if (c.contains("title")) {
        const TextStyle st = style(28, 700, Align::Left, -.01f);
        Node* h1 = in->add(group(0, 0));
        const float b = baseline(10.5f * kNormalLine + 8, 28, 28 * 1.2f);
        const std::string t = c["title"].get<std::string>();
        if (motion::measured()) {
            // Word by word: the title in ink, then the highlight in the accent.
            std::vector<Node*> ws;
            const float space = kit.measure(" ", st);
            float x = 0;
            auto add = [&](const std::string& s, Color col) {
                std::istringstream in(s);
                for (std::string w; in >> w;) {
                    ws.push_back(h1->add(text(x, b, w, st, col)));
                    x += kit.measure(w, st) + space;
                }
            };
            add(t, th.text);
            if (c.contains("highlight")) add(c["highlight"].get<std::string>(), th.accent);
            motion::words(ws, kit.S0 + .2f, motion::Mech::Rise, 18);
        } else {
            h1->add(text(0, b, t, st, th.text));
            if (c.contains("highlight"))
                h1->add(text(kit.measure(t + " ", st), b, c["highlight"].get<std::string>(), st, th.accent));
            enter(h1, kit.S0 + .2f, 0, 12, .5f);
        }
    }
    if (c.contains("chip")) {
        const auto& ch = c["chip"];
        float w = 0;
        Node* g = chip(kit, in, 0, 34, ch["text"].get<std::string>(), ch.value("tone", "good"), 14, 14, 8, &w);
        g->x = kBodyW - w;
        pop(g, kit.at(ch.value("at", json()), 1));
    }
}

void intro(Kit& kit, Node* sc, const json& c, const Logos& lg) {
    const Theme& th = kit.th;
    // Soft accent glow behind the logo (CSS radial-gradient(circle, accent, transparent 62%) in 900x900),
    // rasterized once: a full-screen gradient per frame costs ~280 ms on the raster backend.
    Node glow;
    glow.kind = NodeKind::Glow;
    glow.fill = th.accent.alpha(th.dark ? .10f : .07f);
    glow.x = glow.y = 450;
    // Glow stops at 60% of its radius; scale so it ends at 62% of the farthest corner like the CSS.
    glow.w = glow.h = 450 * 1.41421f * .62f / .6f;
    Node* glowG = sc->add(imageNode(kW / 2 - 450, kH / 2 - 450, 900, 900, rasterize(glow, 0, 0, 900, 900, kit.pixelScale, kit.fonts)));
    glowG->ox = 450; glowG->oy = 450;
    glowG->opacity.fromTo(0, 1, kit.S0 + .05f, 1.4f, ease::p2Out);
    glowG->scale.fromTo(.6f, 1, kit.S0 + .05f, 1.4f, ease::p2Out);

    const bool sub = c.contains("sub");
    if (lg.brand.image) {
        const float h = 118, w = h * lg.brand.aspect;
        const float y = kH / 2 - h / 2 - (sub ? 26 : 0);
        Node* g = sc->add(group(kW / 2 - w / 2, y));
        g->add(imageNode(0, 0, w, h, lg.brand.image));
        g->ox = w * (lg.brandReveal > 0 ? .08f : .5f); g->oy = h / 2;
        if (motion::measured()) {
            // Arrives oversized and comes down; leaves by diving into the cut at a constant octave rate.
            motion::Pose p;
            p.scale = 1.86f; p.blur = 6;
            motion::arrive(g, kit.S0 + .15f, p, 20, motion::fn(motion::snapSettle), 4);
            motion::dive(g, kit.B[kit.k + 1] - motion::F(9));
        } else {
            g->opacity.fromTo(0, 1, kit.S0 + .15f, .7f, ease::p3Out);
            g->scale.fromTo(.82f, 1, kit.S0 + .15f, .7f, ease::p3Out);
        }
        if (lg.brandReveal > 0) {
            g->clip = true;
            g->clipX = 0; g->clipY = -10; g->clipH = h + 20;
            g->clipW = w;
            g->clipWidth.fromTo(w * lg.brandReveal, w, kit.S0 + .85f, .9f, ease::p2InOut);
        }
    }
    if (sub) {
        const TextStyle st = style(14, 600, Align::Center, .2f);
        Node* s = label(sc, kW / 2, kH / 2 + 59 + 38 - 26, upper(c["sub"].get<std::string>()), st, th.text3);
        enter(s, kit.S0 + 1.5f, 0, 10, .5f);
    }
}

void outro(Kit& kit, Node* sc, const json& c, const Logos& lg) {
    const Theme& th = kit.th;
    Node* t = label(sc, kW / 2, 270, c.value("text", ""), style(72, 700, Align::Center, -.02f), th.text, 72 * kNormalLine);
    enter(t, kit.S0 + .2f, 0, 20, .7f, ease::p3Out);
    // Logos row: brand × client, centred.
    const float hb = 34, hc = 34 * lg.clientScale;
    const TextStyle xs = style(18, 400);
    const float wb = lg.brand.image ? hb * lg.brand.aspect : 0, wc = lg.client.image ? hc * lg.client.aspect : 0;
    const float wx = (wb > 0 && wc > 0) ? kit.measure("\xc3\x97", xs) + 40 : 0;
    float x = kW / 2 - (wb + wx + wc) / 2;
    const float cy = 400 + 17;
    Node* row = sc->add(group());
    if (wb > 0) { row->add(imageNode(x, cy - hb / 2, wb, hb, lg.brand.image)); x += wb; }
    if (wx > 0) { row->add(text(x + 20, cy + 6, "\xc3\x97", xs, th.text3)); x += wx; }
    if (wc > 0) row->add(imageNode(x, cy - hc / 2, wc, hc, lg.client.image));
    row->opacity.fromTo(0, 1, kit.S0 + .6f, .6f, ease::p1Out);
}

// The tab itself, with a camera that moves to each focus box (bbox in the 1280-wide
// capture's pixels), outlines it, dims the rest and shows an optional callout.
void slideScene(Kit& kit, Node* sc, const json& c, const LoadedImage& img, float S1) {
    const Theme& th = kit.th;
    const float iw = kW, ih = kW / img.aspect;
    Node* cam = sc->add(group());
    cam->add(imageNode(0, 0, iw, ih, img.image));
    cam->scale.fromTo(1.04f, 1, kit.S0, 1.2f, ease::p2Out);
    const auto& focus = c.value("focus", json::array());
    const float zmax = c.value("zoom_max", 1.7f);
    std::vector<float> T;
    for (size_t i = 0; i < focus.size(); ++i) T.push_back(kit.at(focus[i].value("at", json()), 1.5f + i * 3));
    for (size_t i = 0; i < focus.size(); ++i) {
        const auto& fc = focus[i];
        const auto bb = fc.at("bbox").get<std::vector<float>>();
        const float bx = bb[0], by = bb[1], bw = bb[2], bh = bb[3];
        const float z = std::clamp(std::min(kW * .62f / bw, kH * .58f / bh), 1.f, zmax);
        const bool call = fc.contains("callout");
        const float tx = kW / 2 - (bx + bw / 2) * z, ty = (call ? 330.f : 360.f) - (by + bh / 2) * z;
        const float t = T[i];
        if (motion::measured()) {
            // Reframe as a one-frame snap and a decelerating tail.
            motion::snapTo(cam->tx, tx, t, 0);
            motion::snapTo(cam->ty, ty, t, 0);
            motion::snapTo(cam->scale, z, t, 1);
        } else {
            cam->tx.to(tx, t, 1.f, ease::p3InOut, 0);
            cam->ty.to(ty, t, 1.f, ease::p3InOut, 0);
            cam->scale.to(z, t, 1.f, ease::p3InOut, 1);
        }
        // Outline + dimmed surround, in screen space at the end of the move.
        const float L = tx + bx * z - 6, Tp = ty + by * z - 6, W = bw * z + 12, H = bh * z + 12;
        Node* hl = sc->add(group());
        const Color dim = Color{0, 0, 0, .45f};
        hl->add(rect(0, 0, kW, std::max(0.f, Tp), dim));
        hl->add(rect(0, Tp + H, kW, std::max(0.f, kH - Tp - H), dim));
        hl->add(rect(0, Tp, std::max(0.f, L), H, dim));
        hl->add(rect(L + W, Tp, std::max(0.f, kW - L - W), H, dim));
        Node* box = hl->add(rect(L, Tp, W, H, Color{}, 10));
        box->stroke = th.accent; box->strokeW = 3;
        if (call) {
            const TextStyle st = style(16, 600);
            const auto lines = kit.wrap(fc["callout"].get<std::string>(), st, 350);
            const float lh = 16 * 1.35f, ch = lines.size() * lh + 24;
            const float cxp = std::min(kW - 400, std::max(20.f, L)), cyp = std::min(kH - ch - 20, Tp + H + 14);
            Node* cg = hl->add(group(cxp, cyp));
            cg->add(rect(0, 0, 380, ch, th.panel, 10));
            cg->add(rect(0, 0, 3, ch, th.accent));
            for (size_t k = 0; k < lines.size(); ++k) cg->add(text(16, baseline(12 + k * lh, 16, lh), lines[k], st, th.text));
        }
        const float tEnd = (i + 1 < focus.size() ? T[i + 1] : S1 - 1.2f) - .2f;
        hl->opacity.set(0, 0);
        hl->opacity.fromTo(0, 1, t + .9f, .35f, ease::p1Out);
        hl->opacity.to(0, tEnd, .3f, ease::p1Out, 1);
    }
    if (!focus.empty() && c.value("back", true) && motion::measured()) {
        for (Track<float>* tr : {&cam->tx, &cam->ty}) tr->to(0, S1 - 1.3f, motion::F(18), motion::fn(motion::glide), 0);
        cam->scale.to(1, S1 - 1.3f, motion::F(18), motion::fn(motion::glide), 1);
    } else if (!focus.empty() && c.value("back", true)) {
        cam->tx.to(0, S1 - 1.3f, .9f, ease::p2InOut, 0);
        cam->ty.to(0, S1 - 1.3f, .9f, ease::p2InOut, 0);
        cam->scale.to(1, S1 - 1.3f, .9f, ease::p2InOut, 1);
    }
}

}  // namespace

Video buildVideo(const json& plan, const Fonts& fonts, const std::string& baseDir, float pixelScale) {
    const auto& tk = plan.at("theme");
    const Theme th = Theme::make(hexOr(tk, "bg", "#0e0f10"), hexOr(tk, "text", "#f1f1ef"), hexOr(tk, "accent", "#3987e5"),
                                 hexOr(tk, "good", "#2fb344"), hexOr(tk, "bad", "#e66767"), hexOr(tk, "warn", "#fab219"),
                                 tk.contains("panel") ? Color::hex(tk["panel"].get<std::string>()) : Color{},
                                 hexOr(tk, "blue", "#4E9CF5"));
    const json cfg = plan.value("config", json::object());
    motion::style = cfg.value("motion", "classic") == "measured" ? motion::Style::Measured : motion::Style::Classic;
    const bool whips = motion::measured() && cfg.value("transition", "slide") == "whip";
    const auto& scenes = plan.at("scenes");
    const int N = (int)scenes.size();

    Kit kit{fonts, th, Locale::make(cfg.value("locale", "pt-BR")), pixelScale};

    // Timing: narrated scenes last as long as their speech plus padding.
    kit.B = {0};
    std::vector<std::vector<at::Word>> words(N);
    for (int k = 0; k < N; ++k) {
        const auto& c = scenes[k];
        const std::string type = c.value("type", "");
        float d;
        if (c.contains("speech") && !c["speech"].get<std::string>().empty()) {
            words[k] = at::words(c["speech"].get<std::string>(), &d);
            d += timeline::kPad;
        } else {
            d = c.value("duration", type == "outro" ? 4.0f : type == "intro" ? 2.6f : 5.0f);
        }
        kit.B.push_back(std::round((kit.B.back() + d) * 100) / 100);
    }
    const float total = kit.B.back();

    Video v;
    // Logos, rasterized once at the largest size they are drawn.
    Logos lg;
    const json logos = plan.value("logos", json::object());
    auto asset = [&](const std::string& p) {
        const auto s = (std::filesystem::path(baseDir) / std::filesystem::u8path(p)).u8string();
        return std::string(s.begin(), s.end());
    };
    if (logos.contains("brand")) lg.brand = loadImage(asset(logos["brand"]), (int)std::ceil(132 * pixelScale * 1.5f));
    if (logos.contains("brand_mark") || logos.contains("brand"))
        lg.mark = loadImage(asset(logos.value("brand_mark", logos.value("brand", ""))), (int)std::ceil(24 * pixelScale * 2));
    if (logos.contains("client")) lg.client = loadImage(asset(logos["client"]), (int)std::ceil(40 * pixelScale * 2), logos.value("client_color", ""));
    lg.clientScale = logos.value("client_scale", 1.f);
    lg.brandReveal = logos.value("brand_reveal", 0.f);

    v.duration = total;
    v.root = group();
    Node* root = v.root.get();
    Node* bg = cache(root->add(group()));
    bg->add(rect(0, 0, kW, kH, th.bg));
    for (auto [x, y, rx, ry, a] : {std::tuple{kW * .85f, -kH * .1f, 900.f, 500.f, th.dark ? .10f : .07f},
                                   std::tuple{-kW * .1f, kH * 1.1f, 700.f, 420.f, th.dark ? .06f : .04f}}) {
        auto glow = std::make_unique<Node>();
        glow->kind = NodeKind::Glow;
        glow->x = x; glow->y = y; glow->w = rx; glow->h = ry;
        glow->fill = th.accent.alpha(a);
        bg->add(std::move(glow));
    }
    Node* scenesNode = root->add(group());

    // Header: brand mark × client, tabs with a moving indicator, period on the right.
    Node* hdr = root->add(group());
    hdr->opacity.set(0, 0);
    hdr->add(rect(0, 45, kW, 1, th.line));
    float hx = 28;
    const float hm = 18, hc = 17 * lg.clientScale;
    if (lg.mark.image) { hdr->add(imageNode(hx, 23 - hm / 2, hm * lg.mark.aspect, hm, lg.mark.image)); hx += hm * lg.mark.aspect; }
    if (lg.mark.image && lg.client.image) {
        const TextStyle xs = style(11, 400);
        hdr->add(text(hx + 9, 27, "\xc3\x97", xs, th.text3));
        hx += 18 + kit.measure("\xc3\x97", xs);
    }
    if (lg.client.image) { hdr->add(imageNode(hx, 23 - hc / 2, hc * lg.client.aspect, hc, lg.client.image)); hx += hc * lg.client.aspect; }
    const float tabsX = hx + 34;
    std::vector<std::string> tabs;
    for (const auto& s : scenes) {
        const std::string tab = s.value("tab", "");
        if (!tab.empty() && std::find(tabs.begin(), tabs.end(), tab) == tabs.end()) tabs.push_back(tab);
    }
    const TextStyle tabSt = style(11, 500);
    const float tabBase = baseline((46 - 11 * kNormalLine) / 2, 11, 11 * kNormalLine);
    std::vector<Node*> tabNodes;
    std::vector<float> tabLeft, tabWidth;
    float tx = 0;
    for (const auto& t : tabs) {
        const float w = kit.measure(t, tabSt) + 22;
        tabNodes.push_back(hdr->add(text(tabsX + tx + 11, tabBase, t, tabSt, th.text3)));
        tabLeft.push_back(tx + 8);
        tabWidth.push_back(w - 16);
        tx += w;
    }
    Node* tabbar = hdr->add(rect(tabsX, 46 - 1, 1, 2, th.accent, 1));
    if (const auto period = plan.value("header", json::object()).value("period", ""); !period.empty())
        hdr->add(text(kW - 28, baseline((46 - 10.5f * kNormalLine) / 2, 10.5f, 10.5f * kNormalLine), upper(period),
                      style(10.5f, 600, Align::Right, .12f), th.text3));

    for (int k = 0; k < N; ++k) {
        const auto& c = scenes[k];
        const std::string type = c.value("type", "");
        kit.k = k;
        kit.S0 = kit.B[k];
        kit.t0 = kit.S0 + timeline::kLead;
        kit.when = {words[k], kit.t0, k, &kit.warnings};
        const float S1 = kit.B[k + 1];

        Node* sc = scenesNode->add(group());
        v.scenes.push_back(sc);
        sc->opacity.set(0, 0);
        if (motion::measured()) {
            const bool whipIn = whips && k > 0, whipOut = whips && k + 1 < N;
            if (whipIn) motion::whipIn(sc, kit.S0, kW * .45f);
            else {
                motion::Pose in;
                in.dx = 30;
                motion::arrive(sc, kit.S0, in, 12, motion::fn(motion::softLand), 5, false);
            }
            if (whipOut) motion::whipOut(sc, S1, -kW * .55f);
            else if (k + 1 < N) {
                motion::Pose out;
                out.dx = -30;
                motion::depart(sc, S1 - motion::F(13), out);
            }
        } else {
            sc->opacity.fromTo(0, 1, kit.S0, .6f, ease::p3Out);
            sc->tx.fromTo(30, 0, kit.S0, .6f, ease::p3Out);
            if (k + 1 < N) {
                sc->opacity.to(0, S1 - .45f, .45f, ease::p2In, 1);
                sc->tx.to(-30, S1 - .45f, .45f, ease::p2In, 0);
            }
        }
        const bool withHeader = type == "content" || (type == "slide" && c.value("header", false));
        hdr->opacity.to(withHeader ? 1.f : 0.f, kit.S0, .4f, ease::p1Out, 0);
        if (const std::string tab = c.value("tab", ""); !tab.empty()) {
            const size_t ti = std::find(tabs.begin(), tabs.end(), tab) - tabs.begin();
            tabbar->tx.to(tabLeft[ti], kit.S0 + .1f, .5f, ease::p3InOut, 0);
            tabbar->scaleX.to(tabWidth[ti], kit.S0 + .1f, .5f, ease::p3InOut, 1);
            for (size_t j = 0; j < tabNodes.size(); ++j)
                tabNodes[j]->color.to(j == ti ? th.text : th.text3, kit.S0 + .1f, .3f, ease::p1Out, th.text3);
        }

        if (type == "intro") intro(kit, sc, c, lg);
        else if (type == "outro") outro(kit, sc, c, lg);
        else if (type == "slide") slideScene(kit, sc, c, loadImage(asset(c.at("image").get<std::string>()), (int)std::ceil(720 * pixelScale)), S1);
        else if (type == "content") {
            Node* in = sc->add(group(kInnerX, kInnerY));
            if (motion::measured()) {
                // No dead frame: the page creeps toward the viewer for the whole scene.
                in->ox = kBodyW / 2; in->oy = (kBodyTop + kBodyH) / 2;
                motion::drift(in->scale, 1, 1.012f, kit.S0, S1);
            }
            titleBlock(kit, in, c);
            Node* body = in->add(group(0, kBodyTop));
            layoutBox(kit, body, c.at("body"), {0, 0, kBodyW, kBodyH});
        } else {
            throw std::runtime_error("scene " + std::to_string(k + 1) + ": unsupported type '" + type + "'");
        }
    }

    Node* prog = root->add(rect(0, kH - 3, kW, 3, th.accent));
    prog->scaleX.fromTo(0, 1, 0, total, ease::linear);
    prepare(*root, fonts);
    v.B = kit.B;
    v.twos = cfg.value("clock", "smooth") == "twos";
    v.warnings = std::move(kit.warnings);
    return v;
}
