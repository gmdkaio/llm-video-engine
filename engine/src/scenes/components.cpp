// Components of plan@1 (see skill/COMPONENTS.md). Each one draws into its rect
// and animates against the scene's `at` times. Geometry and timing follow the
// reference scenes they were generalized from.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

#include "scenes/kit.h"

namespace {

const json kNull;
const json& field(const json& o, const char* key) {
    auto it = o.find(key);
    return it == o.end() ? kNull : *it;
}
std::string str(const json& o, const char* key, const std::string& d = "") { return o.contains(key) ? o[key].get<std::string>() : d; }

struct Scale { double min, max, step; };

double niceStep(double v) {
    const double m = v * 1.08;
    const double p = std::pow(10.0, std::floor(std::log10(m > 0 ? m : 1)));
    for (double x : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m / (x * p) <= 5) return x * p;
    return 10 * p;
}

Scale autoScale(const std::vector<double>& vals) {
    double mx = 0, mn = 0;
    for (double v : vals) { mx = std::max(mx, v); mn = std::min(mn, v); }
    const double span = std::max({mx, -mn, (mx - mn) * 0.6});
    const double step = niceStep(span);
    double top = std::ceil(mx * 1.08 / step) * step;
    if (top == 0) top = step;
    const double bottom = mn < 0 ? -std::ceil(-mn * 1.08 / step) * step : 0;
    return {bottom, top, step};
}

Scale scaleFor(const json& c, const std::vector<double>& vals) {
    Scale s = autoScale(vals);
    if (const json& y = field(c, "y"); y.is_object()) {
        if (y.contains("min")) s.min = y["min"].get<double>();
        if (y.contains("max")) s.max = y["max"].get<double>();
        if (y.contains("step")) s.step = y["step"].get<double>();
    }
    return s;
}

// Gridlines and labels at each step; `inset` is the gap between the top gridline and the top.
void axis(Kit& kit, Node* g, const json& c, const Scale& s, float L, float R, const std::function<float(double)>& y) {
    const json& yc = field(c, "y");
    const double div = yc.is_object() ? yc.value("divide", 1.0) : 1.0;
    const int dec = yc.is_object() && yc.contains("decimals") ? yc["decimals"].get<int>() : axisDecimals(s.step / div);
    const std::string suf = yc.is_object() ? yc.value("suffix", "") : "";
    for (double v = s.min; v <= s.max + 1e-9; v += s.step) {
        const float yy = y(v);
        g->add(line(L, yy, R, yy, kit.th.grid, 1));
        g->add(text(L - 8, yy + 3, kit.loc.nf(v / div, dec) + suf, style(9.5f, 400, Align::Right), kit.th.text3));
    }
}

void splitMarker(Kit& kit, Node* g, const json& split, float x, float yTop, float yBottom) {
    Node* sp = g->add(line(x, yTop, x, yBottom, kit.th.line, 1));
    sp->dash = {4, 4};
    fade(sp, kit.t0 + .6f, .4f);
    const auto labels = split.value("labels", std::vector<std::string>{});
    const TextStyle st = style(9.5f, 700, Align::Right, .12f);
    if (labels.size() > 0) fade(g->add(text(x - 8, yTop + 12, labels[0], st, kit.th.text3)), kit.t0 + .6f, .4f);
    if (labels.size() > 1) {
        TextStyle sr = st;
        sr.align = Align::Left;
        fade(g->add(text(x + 8, yTop + 12, labels[1], sr, kit.th.text3)), kit.t0 + .6f, .4f);
    }
}

// Rich text: <b>..</b> spans, greedy wrapping by words.
struct Span { std::string text; bool bold; };
std::vector<Span> parseRuns(const std::string& s) {
    std::vector<Span> out;
    bool bold = false;
    size_t i = 0;
    while (i < s.size()) {
        const size_t open = s.find('<', i);
        const std::string chunk = s.substr(i, open == std::string::npos ? std::string::npos : open - i);
        if (!chunk.empty()) out.push_back({chunk, bold});
        if (open == std::string::npos) break;
        const size_t close = s.find('>', open);
        const std::string tag = s.substr(open + 1, close - open - 1);
        if (tag == "b" || tag == "strong") bold = true;
        else if (tag == "/b" || tag == "/strong") bold = false;
        i = close + 1;
    }
    return out;
}

// Lays out rich text in g, top-left at (0, 0). Returns the height.
float richText(Kit& kit, Node* g, const std::string& s, float font, float width, Color normal, Color strong, float lineH) {
    const TextStyle reg = style(font, 400), bold = style(font, 600);
    struct W { std::string t; bool b; };
    std::vector<W> words;
    for (const auto& sp : parseRuns(s)) {
        std::string cur;
        for (char ch : sp.text) {
            if (ch == ' ') { if (!cur.empty()) words.push_back({cur, sp.bold}); cur.clear(); }
            else cur += ch;
        }
        if (!cur.empty()) words.push_back({cur, sp.bold});
    }
    const float space = kit.measure(" ", reg);
    float x = 0;
    int lineNo = 0;
    for (const auto& w : words) {
        const float ww = kit.measure(w.t, w.b ? bold : reg);
        if (x > 0 && x + ww > width) { x = 0; ++lineNo; }
        g->add(text(x, baseline(lineNo * lineH, font, lineH), w.t, w.b ? bold : reg, w.b ? strong : normal));
        x += ww + space;
    }
    return (lineNo + 1) * lineH;
}

// ---------- text-like ----------

void number(Kit& kit, Node* g, const json& c, Rect r) {
    const float font = c.value("font", 66.f);
    const std::string fmt = c.value("format", "num");
    const int dec = c.value("decimals", -1);
    const std::string pre = c.value("prefix", ""), suf = c.value("suffix", "");
    TextStyle st = style(font, font >= 40 ? 800 : 700, Align::Left, -.025f);
    st.tnum = true;
    const Locale& loc = kit.loc;
    counter(g, r.x, baseline(r.y, font, font), 0, c.at("value").get<double>(), kit.at(field(c, "at"), 1), c.value("dur", 1.1f),
            [loc, fmt, dec, pre, suf](double v) { return loc.value(v, fmt, dec, pre, suf); }, st, kit.tone(c.value("tone", "text")));
}

float textHeight(const Kit& kit, const json& c, float w) {
    const float font = c.value("font", 14.f);
    const float width = std::min(w, c.value("width", w));
    std::string plain;
    for (const auto& sp : parseRuns(c["text"].get<std::string>())) plain += sp.text;
    return (float)std::max<size_t>(1, kit.wrap(plain, style(font, 500), width).size()) * font * 1.55f;
}

void textBlock(Kit& kit, Node* parent, const json& c, Rect r) {
    const float font = c.value("font", 14.f);
    Node* g = parent->add(group(r.x, r.y));
    richText(kit, g, c["text"].get<std::string>(), font, std::min(r.w, c.value("width", r.w)), kit.tone(c.value("tone", "text2")),
             kit.th.text, font * 1.55f);
    g->ox = r.w / 2;
    pop(g, kit.at(field(c, "at"), 2), 10, 1);
}

constexpr float kChipH = 12.5f * kNormalLine + 12;

void chips(Kit& kit, Node* parent, const json& c, Rect r) {
    const bool col = c.value("dir", "row") == "column";
    float x = r.x, y = col ? r.y : r.y + std::max(0.f, (r.h - kChipH) / 2);
    int i = 0;
    for (const auto& ch : c["items"]) {
        float w = 0;
        Node* g = chip(kit, parent, x, y, ch["text"].get<std::string>(), ch.value("tone", "good"), 12.5f, 11, 6, &w);
        pop(g, kit.at(field(ch, "at"), 2 + i * 1.2f));
        if (col) y += kChipH + 8;
        else x += w + 8;
        ++i;
    }
}

void kv(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const float h = intrinsicHeight(kit, c, r.w);
    Node* g = parent->add(group(r.x, r.y));
    g->add(rect(0, 0, r.w, h, th.accent, 12));
    g->add(rect(3, 0, r.w - 3, h, th.panel, 10));
    float y = 12;
    if (c.contains("title")) {
        label(g, 16, y, upper(c["title"].get<std::string>()), style(10.5f, 700, Align::Left, .12f), th.accent);
        y += 10.5f * kNormalLine;
    }
    for (const auto& row : c["rows"]) {
        y += 7;
        label(g, 16, y, row[0].get<std::string>(), style(13, 400), th.text2);
        if (row.size() > 1) label(g, r.w - 16, y, row[1].get<std::string>(), style(13, 800, Align::Right), th.text);
        y += 13 * kNormalLine;
    }
    g->ox = r.w / 2; g->oy = h / 2;
    pop(g, kit.at(field(c, "at"), 3), 14, 1);
}

void list(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& items = c["items"];
    const int n = (int)items.size();
    const float rh = std::floor(std::min(r.h, 468.f) / std::max(1, n));
    std::vector<float> T;
    for (int i = 0; i < n; ++i) T.push_back(kit.at(field(items[i], "at"), 3 + i * 2.f));
    const bool numbered = c.value("numbered", true);
    for (int i = 0; i < n; ++i) {
        const auto& it = items[i];
        const float h = rh - 12;
        Node* g = parent->add(group(r.x, r.y + i * rh));
        Node* bg = g->add(rect(0, 0, r.w, h, th.panel, 12));
        bg->stroke = th.accent.alpha(0);
        bg->strokeW = 1.5f;
        Node* bar = g->add(rect(0, 0, 4, h, th.accent));
        float x = 30;
        if (numbered) {
            const std::string num = (i + 1 < 10 ? "0" : "") + std::to_string(i + 1);
            g->add(text(x, baseline((h - 30 * kNormalLine) / 2, 30, 30 * kNormalLine), num, style(30, 800), th.accent));
            x += std::max(44.f, kit.measure(num, style(30, 800))) + 22;
        }
        const bool sub = it.contains("text");
        const float blockH = 21 * kNormalLine + (sub ? 5 + 13 * kNormalLine : 0);
        const float top = (h - blockH) / 2;
        label(g, x, top, it["title"].get<std::string>(), style(21, 700), th.text);
        if (sub) label(g, x, top + 21 * kNormalLine + 5, it["text"].get<std::string>(), style(13, 400), th.text2);
        enter(g, T[i], 60, 0, .55f, ease::p3Out);
        bar->oy = h / 2;
        bar->scaleY.fromTo(0, 1, T[i] + .1f, .4f, ease::p2Out);
        bg->strokeColor.fromTo(th.accent.alpha(0), th.accent.alpha(.55f), T[i], .3f, ease::p1Out);
        if (i + 1 < n) {
            bg->strokeColor.to(th.accent.alpha(0), T[i + 1], .4f, ease::p1Out, th.accent.alpha(0));
            bar->opacity.to(.35f, T[i + 1], .4f, ease::p1Out, 1);
        }
    }
}

// ---------- charts ----------

void bars(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto cats = c.at("cats").get<std::vector<std::string>>();
    const auto V = c.at("values").get<std::vector<double>>();
    const int n = (int)V.size();
    std::vector<double> S(n, 0);
    Color stackColor = th.warn;
    std::string stackLabel;
    if (const json& st = field(c, "stack"); st.is_object()) {
        S = st["values"].get<std::vector<double>>();
        S.resize(n, 0);
        stackColor = kit.tone(st.value("tone", "warn"));
        stackLabel = st.value("label", "");
    }
    std::vector<double> ref;
    if (const json& rf = field(c, "ref"); rf.is_object()) ref = rf["values"].get<std::vector<double>>();

    const bool showAxis = c.value("axis", true);
    const json& yc = field(c, "y");
    const bool suffix = yc.is_object() && yc.contains("suffix");
    const float W = r.w, H = r.h - 30, L = showAxis ? (suffix ? 48.f : 40.f) : 0.f;
    std::vector<double> all;
    for (int i = 0; i < n; ++i) all.push_back(V[i] + S[i]);
    all.insert(all.end(), ref.begin(), ref.end());
    const Scale sc = scaleFor(c, all);
    // Captured by value: annotations call these after this function returns.
    auto y = [sc, H](double v) { return (float)(H - (v - sc.min) / (sc.max - sc.min) * (H - 30)); };
    const float bw = (W - L) / n;

    Node* g = parent->add(group(r.x, r.y));
    if (showAxis) axis(kit, g, c, sc, L, W, y);
    // Bars rise from zero, or from the bottom of the scale when it does not include zero.
    const double base = std::clamp(0.0, sc.min, sc.max);
    if (!showAxis) g->add(line(L, y(base), W, y(base), th.line, 1));
    if (showAxis && sc.min < 0) g->add(line(L, y(0), W, y(0), th.text3, 1));

    std::set<int> hl;
    const json& h = field(c, "highlight");
    if (h.is_null() && c.contains("highlight")) {}
    else if (h.is_array()) for (int i : h) hl.insert(i);
    else hl.insert(n - 1);
    std::set<int> proj;
    for (int i : c.value("projection", std::vector<int>{})) proj.insert(i);
    const bool neutral = c.value("color", "accent") == "neutral";
    const double ldiv = c.value("label_divide", 1.0);
    const bool labels = c.value("labels", true);
    const float T0 = kit.at(field(c, "at"), .3f);
    const float Tp = c.contains("at_projection") ? kit.at(c["at_projection"], 2) : -1;

    ChartInfo ci;
    ci.layer = g;
    ci.left = L; ci.right = W; ci.top = y(sc.max); ci.bottom = H; ci.slot = bw;
    ci.x = [L, bw](int i) { return L + i * bw + bw / 2; };
    ci.y = y;
    ci.values = V;
    std::vector<Node*> cols;
    for (int i = 0; i < n; ++i) {
        const double v = V[i];
        const float x = L + i * bw + bw * .2f, w = bw * .6f;
        const bool hi = hl.count(i) > 0, isP = proj.count(i) > 0;
        Node* col = g->add(group());
        cols.push_back(col);
        col->add(text(x + w / 2, H + 16, cats[i], style(n > 14 ? 8.5f : 10.5f, hi ? 700 : 500, Align::Center), hi ? th.text : th.text3));
        const float tt = isP && Tp >= 0 ? Tp : T0 + i * std::min(.08f, 1.2f / n);
        const float top = std::min(y(v), y(base)), bh = std::max(.5f, std::fabs(y(base) - y(v)));
        Node* bar = col->add(rect(x, top, w, bh, Color{}, 3));
        if (isP) {
            bar->shader = neutral ? hatchShader(th.text.alpha(.05f), th.text.alpha(.28f)) : hatchShader(th.accent.alpha(.18f), th.accent);
            bar->stroke = neutral ? th.neutral : th.accent;
            bar->strokeW = 1.5f;
        } else {
            bar->fill = neutral ? th.neutral2 : (hi ? th.accent : th.accentDim);
        }
        grow(bar, tt, v < 0, isP ? 1.f : .6f);
        ci.bars.push_back(bar);
        ci.projection.push_back(isP);
        double topV = v;
        if (S[i] > 0) {
            Node* sb = col->add(rect(x, y(v + S[i]), w, y(v) - y(v + S[i]), stackColor, 3));
            grow(sb, T0 + .45f, false, .5f);
            if (!stackLabel.empty() && y(v) - y(v + S[i]) > 16)
                fade(col->add(text(x + w / 2, (y(v) + y(v + S[i])) / 2 + 4, stackLabel, style(10.5f, 800, Align::Center), th.bg)), T0 + .8f, .3f);
            topV = v + S[i];
        }
        ci.tops.push_back(topV);
        if (labels) {
            const int dec = c.contains("label_decimals") ? c["label_decimals"].get<int>() : (std::fabs(topV / ldiv) < 10 && ldiv == 1 ? 1 : 0);
            const bool big = hi || isP;
            const float fs = S[i] > 0 || hi ? (hi ? 13.f : 11.f) : (n > 14 ? 8.5f : 10.5f);
            Node* lb = col->add(text(x + w / 2, topV < 0 ? y(topV) + 14 : y(topV) - 8, kit.loc.nf(topV / ldiv, dec),
                                     style(fs, big ? 700 : 600, Align::Center), big ? th.text : th.text2));
            fade(lb, tt + .45f, .3f);
            ci.labels.push_back(lb);
        } else {
            ci.labels.push_back(nullptr);
        }
    }
    if (!ref.empty()) {
        Node* clip = g->add(group());
        clip->clip = true;
        clip->clipW = 0; clip->clipH = H + 40;
        clip->clipWidth.fromTo(0, W, kit.at(field(c["ref"], "at"), 2), 1.2f, ease::p2InOut);
        auto p = std::make_unique<Node>();
        p->kind = NodeKind::Polyline;
        for (int i = 0; i < (int)ref.size(); ++i) { p->pts.push_back(L + i * bw + bw * .5f); p->pts.push_back(y(ref[i])); }
        p->stroke = th.neutral; p->strokeW = 2; p->dash = {5, 5};
        clip->add(std::move(p));
    }
    if (const json& sp = field(c, "split"); sp.is_object())
        splitMarker(kit, g, sp, L + (sp["after"].get<int>() + 1) * bw, 18, H);
    if (const json& f = field(c, "focus"); f.is_object()) {
        const float tf = kit.at(field(f, "at"), 6);
        for (int i = 0; i < n; ++i)
            if (i != f["idx"].get<int>()) cols[i]->opacity.to(.35f, tf, .4f, ease::p1Out, 1);
    }
    if (c.contains("id")) kit.charts[c["id"].get<std::string>()] = ci;
}

void lines(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto cats = c.at("cats").get<std::vector<std::string>>();
    const int n = (int)cats.size();
    const auto& series = c.at("series");
    std::vector<double> all;
    for (const auto& s : series) for (double v : s["values"]) all.push_back(v);
    const Scale sc = scaleFor(c, all);
    const float L = 60, W = r.w - 150, H = r.h - 30;
    const float bw = (W - L) / std::max(1, n - 1);
    auto y = [sc, H](double v) { return (float)(H - (v - sc.min) / (sc.max - sc.min) * (H - 40)); };
    auto X = [L, bw](int i) { return L + i * bw; };

    Node* g = parent->add(group(r.x, r.y));
    axis(kit, g, c, sc, L, W, y);
    for (int i = 0; i < n; ++i) g->add(text(X(i), H + 18, cats[i], style(10.5f, 400, Align::Center), th.text3));

    const float TN = kit.at(field(c, "at_series"), 6);
    if (const json& z = field(c, "zero"); z.is_object() && sc.min < 0) {
        Node* neg = g->add(rect(L, y(0), W - L, y(sc.min) - y(0), th.bad.alpha(.09f)));
        neg->opacity.fromTo(0, 1, kit.at(field(z, "at"), 6), .8f, ease::p1Out);
        g->add(line(L, y(0), W, y(0), th.bad, 1.6f));
        if (z.contains("label")) g->add(text(L + 6, y(0) - 7, z["label"].get<std::string>(), style(10.5f, 700), th.bad));
    }
    const int ri = c.value("actual_until", -1);
    if (const json& sp = field(c, "split"); sp.is_object()) splitMarker(kit, g, sp, X(sp["after"].get<int>()) + bw / 2, 20, H);

    auto pathOf = [&](const std::vector<double>& vv, int from, int to) {
        std::string d;
        for (int i = from; i <= to; ++i) d += (i == from ? "M" : "L") + std::to_string(X(i)) + " " + std::to_string(y(vv[i])) + " ";
        return d;
    };
    ChartInfo ci;
    ci.layer = g;
    ci.left = L; ci.right = W; ci.top = y(sc.max); ci.bottom = H; ci.slot = bw;
    ci.x = X;
    ci.y = y;
    if (ri >= 0 && !series.empty()) {
        const auto vv = series[0]["values"].get<std::vector<double>>();
        Node* p = g->add(pathNode(pathOf(vv, 0, ri), Color{}, th.text, 3));
        p->trim.fromTo(0, 1, kit.t0 + .4f, 1.2f, ease::p2InOut);
        for (int i = 0; i <= ri; ++i) {
            Node* d = g->add(arc(X(i), y(vv[i]), 2, 0, 360, th.text, 4));
            fade(d, kit.t0 + .5f + i * .3f, .2f);
        }
        if (c.contains("actual_label"))
            fade(g->add(text(X(ri), y(vv[ri]) - 14, c["actual_label"].get<std::string>(), style(12, 800, Align::Center), th.text)), kit.t0 + 1.5f, .3f);
    }
    int j = 0;
    for (const auto& s : series) {
        const auto vv = s["values"].get<std::vector<double>>();
        const Color col = kit.tone(s.value("tone", "accent"));
        Node* p = g->add(pathNode(pathOf(vv, std::max(0, ri), n - 1), Color{}, col, 2.4f));
        p->trim.fromTo(0, 1, TN + j * .35f, 1.7f, ease::p2InOut);
        const int li = n - 1;
        Node* end = g->add(group());
        end->add(arc(X(li), y(vv[li]), 2.5f, 0, 360, col, 5));
        end->add(text(X(li) + 12, y(vv[li]) + 4, s.value("name", "") + " " + (vv[li] < 0 ? "\xe2\x88\x92" : "") + kit.loc.nf(std::fabs(vv[li]), 0),
                      style(12.5f, 800), col));
        fade(end, TN + j * .35f + 1.6f, .3f);
        if (s.contains("id")) { ci.series[s["id"].get<std::string>()] = vv; ci.seriesPaths[s["id"].get<std::string>()] = p; }
        if (j == 0) ci.values = vv;
        ++j;
    }
    if (c.contains("id")) kit.charts[c["id"].get<std::string>()] = ci;
}

void stackbar(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& items = c.at("items");
    double sum = 0;
    for (const auto& it : items) sum += it[1].get<double>();
    const double mx = c.value("max", sum * 1.08);
    const float SW = (r.w - 128) / (float)mx;
    const float y0 = r.y + r.h - 128;  // drawing box is 128 tall, anchored at the bottom
    Node* g = parent->add(group(r.x, y0));
    const float TA = kit.at(field(c, "at"), 1);
    const double div = c.value("label_divide", 1.0);
    const int dec = c.value("label_decimals", 1);
    const int n = (int)items.size();
    float xx = 0, hlx = -1;
    for (int i = 0; i < n; ++i) {
        const double v = items[i][1].get<double>();
        const bool hl = items[i].size() > 2 && items[i][2].get<bool>();
        if (hl && hlx < 0) hlx = xx;
        const float w = (float)v * SW;
        Node* seg = g->add(rect(xx, 34, std::max(1.f, w - 3), 52, hl ? th.accent : th.accentDim, 4));
        const float tt = TA + i * .16f;
        growX(seg, tt, .32f, ease::p2Out);
        Node* l1 = g->add(text(xx + (w - 3) / 2, 104, items[i][0].get<std::string>(), style(10.5f, hl ? 700 : 500, Align::Center), hl ? th.text : th.text3));
        Node* l2 = g->add(text(xx + (w - 3) / 2, 65, kit.loc.nf(v / div, dec), style(w > 58 ? 12 : 10, 700, Align::Center),
                               hl ? kit.onTone("accent") : th.text));
        fade(l1, tt + .25f, .25f);
        fade(l2, tt + .25f, .25f);
        xx += w;
    }
    if (const json& t = field(c, "target"); t.is_object()) {
        const float ox = t["value"].get<float>() * SW;
        Node* ol = g->add(line(ox, 22, ox, 96, th.text, 2));
        ol->dash = {4, 3};
        fade(ol, TA + .2f, .4f);
        if (t.contains("label")) fade(g->add(text(ox, 14, t["label"].get<std::string>(), style(11, 600, Align::Center), th.text2)), TA + .2f, .4f);
    }
    if (c.contains("end_label")) fade(g->add(text(xx + 8, 66, c["end_label"].get<std::string>(), style(16, 800), th.accent)), TA + n * .16f + .2f, .4f);
    if (c.contains("note") && hlx >= 0)
        fade(g->add(text(hlx, 124, c["note"].get<std::string>(), style(10.5f, 700, Align::Left, .06f), th.accent)), TA + n * .16f + .6f, .4f);
}

void ruler(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& rows = c.at("rows");
    double maxRatio = 1;
    for (const auto& row : rows) maxRatio = std::max(maxRatio, row["value"].get<double>() / row["target"].get<double>());
    const float X0 = r.w * .42f, TW = r.w * .505f, MX = (float)std::max(1.15, maxRatio * 1.1);
    const float u = TW / MX, PX = X0 + u;
    Node* g = parent->add(group(r.x, r.y));
    int i = 0;
    for (const auto& row : rows) {
        const float y = 14 + i * 94.f;
        const float T = kit.at(field(row, "at"), 1 + i * 3.f);
        const double ratio = row["value"].get<double>() / row["target"].get<double>();
        g->add(text(0, y + 22, row["label"].get<std::string>(), style(15, 700), th.text));
        if (row.contains("sub")) g->add(text(0, y + 42, row["sub"].get<std::string>(), style(11.5f, 400), th.text3));
        const std::string fmt = row.value("format", "num");
        const int dec = row.value("decimals", -1);
        const Locale& loc = kit.loc;
        counter(g, r.w * .21f, y + 36, 0, row["value"].get<double>(), T, 1.1f, [loc, fmt, dec](double v) { return loc.value(v, fmt, dec); },
                style(34, 700, Align::Left, -.02f), th.text);
        g->add(rect(X0, y + 8, TW, 36, th.grid, 6));
        Node* a = g->add(rect(X0, y + 8, u * (float)std::min(ratio, 1.0), 36, th.accentDim, 6));
        growX(a, T, .7f, ease::p2In);
        if (ratio > 1) {
            Node* b = g->add(rect(PX - 4, y + 8, u * (float)(ratio - 1) + 4, 36, th.accent, 6));
            growX(b, T + .7f, .75f, ease::p3Out);
        }
        Node* mk = g->add(line(PX, y + 1, PX, y + 51, th.text, 2));
        mk->dash = {4, 3};
        fade(mk, kit.S0 + .5f + i * .2f, .4f);
        if (row.contains("target_label"))
            fade(g->add(text(PX, y + 68, row["target_label"].get<std::string>(), style(11, 600, Align::Center), th.text2)), kit.S0 + .5f + i * .2f, .4f);
        if (row.contains("tag")) {
            // After the bar end, or after the target marker when the value falls short of it.
            Node* tg = g->add(text(X0 + u * (float)std::max(ratio, 1.0) + 12, y + 32, row["tag"].get<std::string>(), style(16, 800), th.accent));
            enter(tg, T + 1.4f, -8, 0);
        }
        ++i;
    }
}

void meter(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const float W = r.w - 48, dw = W / c.at("max").get<float>();
    const float TD = kit.at(field(c, "at"), 6);
    Node* g = parent->add(group(r.x, r.y));
    if (c.contains("label")) g->add(text(0, 16, c["label"].get<std::string>(), style(12, 600), th.text2));
    g->add(rect(0, 26, W, 34, th.grid, 6));
    Node* bar = g->add(rect(0, 26, c["value"].get<float>() * dw, 34, kit.tone(c.value("tone", "neutral")), 6));
    growX(bar, TD, .9f, ease::p2Out);
    if (const json& m = field(c, "marker"); m.is_object()) {
        const float mx = m["value"].get<float>() * dw;
        Node* ml = g->add(line(mx, 20, mx, 66, th.text, 2));
        ml->dash = {3, 3};
        fade(ml, TD + .8f, .3f);
        if (m.contains("label")) fade(g->add(text(mx, 82, m["label"].get<std::string>(), style(10.5f, 400, Align::Center), th.text3)), TD + .8f, .3f);
    }
    if (c.contains("value_label")) fade(g->add(text(12, 49, c["value_label"].get<std::string>(), style(14, 800), th.bg)), TD + .6f, .3f);
    if (c.contains("note")) fade(g->add(text(W, 104, c["note"].get<std::string>(), style(11.5f, 600, Align::Right), th.text2)), TD + 1, .3f);
}

void rings(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& rs = c.at("rings");
    const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    float radius = std::min(r.w, r.h) / 2 - 22;
    std::vector<float> T;
    for (int i = 0; i < (int)rs.size(); ++i) T.push_back(kit.at(field(rs[i], "at"), 2 + i * 4.f));
    for (int i = 0; i < (int)rs.size(); ++i) {
        const auto& ri = rs[i];
        const Color col = i == 0 ? th.accent : mix(th.accent, th.panel, .45f);
        parent->add(arc(cx, cy, radius, 0, 360, th.grid, 38));
        Node* a = parent->add(arc(cx, cy, radius, 0, 360, col, 38));
        a->trim.set(0, 0);
        a->trim.fromTo(0, ri["pct"].get<float>() / 100, T[i], 1.3f, ease::p2InOut);
        if (ri.contains("label"))
            fade(parent->add(text(cx + 10, cy - radius + 5, ri["label"].get<std::string>(), style(12, 700), i == 0 ? kit.onTone("accent") : th.text)),
                 T[i] + .3f, .3f);
        Node* center = parent->add(group(cx, cy));
        center->add(text(0, 18, ri.value("center", ""), style(64, 800, Align::Center, -.03f), i == 0 ? th.accent : th.text));
        center->add(text(0, 48, ri.value("center_sub", ""), style(12.5f, 600, Align::Center), th.text2));
        center->opacity.fromTo(0, 1, T[i] + .8f, .5f, ease::back(1.5f));
        center->scale.fromTo(.85f, 1, T[i] + .8f, .5f, ease::back(1.5f));
        if (i + 1 < (int)rs.size()) center->opacity.to(0, T[i + 1] + .3f, .3f, ease::p1Out, 1);
        radius -= 68;
    }
}

void gauge(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const float mn = c.value("min", 0.f), mx = c.value("max", 4.f);
    const float from = c.at("from").get<float>(), to = c.at("to").get<float>();
    const float cx = r.x + r.w / 2, cy = r.y + 228, rad = std::min(172.f, r.w / 2 - 70);
    auto th_ = [&](float v) { return (float)M_PI * (1 - (v - mn) / (mx - mn)); };
    auto P = [&](float v, float rr) { return std::pair<float, float>{cx + rr * std::cos(th_(v)), cy - rr * std::sin(th_(v))}; };
    auto toneAt = [&](float v) {
        for (const auto& b : c.value("bands", json::array()))
            if (v >= b[0].get<float>() && v <= b[1].get<float>()) return kit.tone(b[2].get<std::string>());
        return th.accent;
    };
    for (const auto& b : c.value("bands", json::array())) {
        const auto p0 = P(b[0].get<float>(), rad), p1 = P(b[1].get<float>(), rad);
        Node* a = parent->add(pathNode("M" + std::to_string(p0.first) + " " + std::to_string(p0.second) + " A" + std::to_string(rad) + " " +
                                           std::to_string(rad) + " 0 0 1 " + std::to_string(p1.first) + " " + std::to_string(p1.second),
                                       Color{}, kit.tone(b[2].get<std::string>()), 26));
        a->opacity.set(.88f, 0);
    }
    const std::string ts = c.value("tick_suffix", "");
    for (const auto& tk : c.value("ticks", json::array())) {
        const auto q = P(tk.get<float>(), rad + 30);
        parent->add(text(q.first, q.second + 4, kit.loc.nf(tk.get<double>(), 0) + ts, style(11, 600, Align::Center), th.text3));
    }
    auto rot = [&](float v) { return (v - mn) / (mx - mn) * 180 - 90; };
    Node* nd = parent->add(group(cx, cy));
    Node* needle = nd->add(line(0, 0, 0, -rad + 28, th.text, 5));
    needle->roundCap = true;
    Node* hub = nd->add(arc(0, 0, 5, 0, 360, th.text, 10));
    (void)hub;
    const float TA = kit.at(field(c, "at"), 1.5f);
    nd->rotation.fromTo(rot(from), rot(to), TA, 1.5f, ease::p3InOut);
    const int dec = c.value("decimals", 2);
    const std::string suf = c.value("suffix", "");
    const Locale& loc = kit.loc;
    Node* vt = parent->add(text(cx, cy + 70, "", style(48, 800, Align::Center, -.02f), toneAt(from)));
    vt->format = [loc, dec, suf](double v) { return loc.nf(v, dec) + suf; };
    vt->value.fromTo(from, to, TA, 1.5f, ease::p3InOut);
    fade(vt, kit.S0 + .5f, .4f);
    vt->color.fromTo(toneAt(from), toneAt(to), TA + 1.1f, .3f, ease::p1Out);
    if (c.contains("from_label")) {
        Node* s1 = parent->add(text(cx, cy + 98, c["from_label"].get<std::string>(), style(12.5f, 600, Align::Center), th.text2));
        fade(s1, kit.S0 + .6f, .4f);
        s1->opacity.to(0, TA + 1, .25f, ease::p1Out, 1);
    }
    if (c.contains("to_label")) {
        Node* s2 = parent->add(text(cx, cy + 98, c["to_label"].get<std::string>(), style(12.5f, 700, Align::Center), toneAt(to)));
        s2->opacity.fromTo(0, 1, TA + 1.25f, .35f, ease::p1Out);
    }
    float y = r.y + 344;
    int i = 0;
    for (const auto& ch : c.value("chips", json::array())) {
        Node* g = chip(kit, parent, r.x, y, ch["text"].get<std::string>(), ch.value("tone", "neutral"));
        pop(g, ch.contains("at") ? kit.at(ch["at"], 3) : TA + 1.6f + i * .35f);
        y += kChipH + 8;
        ++i;
    }
}

void flow(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& src = c.at("sources");
    const double total = src.at("total").get<double>();
    const float k = r.w / 1224;
    const float X0 = 260 * k, XW = 22, X1 = 720 * k, TX = 1018 * k, TW = 168, Y0 = 34 + 14, GAP = 10, TH = 250;
    const float KH = 350 / (float)total;
    const json tank = c.value("tank", json::object());
    const double div = tank.value("divide", 1000.0);
    const std::string fmt = tank.value("format", "mi");
    const int dec = tank.value("decimals", 2);
    const Locale& loc = kit.loc;
    Node* g = parent->add(group(r.x, r.y));
    const float TF = kit.at(field(c, "at_flow"), 1.5f);

    if (src.contains("label"))
        fade(g->add(text(X0 + XW / 2, Y0 - 12, src["label"].get<std::string>(), style(12, 700, Align::Center, .04f), th.text2)), kit.t0 + .2f, .4f);
    float yy = Y0;
    std::vector<Node*> parts;
    int i = 0;
    for (const auto& p : src.at("parts")) {
        const double v = p[1].get<double>();
        const float h = (float)v * KH;
        Node* rr = g->add(rect(X0, yy, XW, h, i == 0 ? th.accent : mix(th.accent, th.panel, .4f)));
        grow(rr, kit.t0 + .3f + i * .2f, true, .6f);
        parts.push_back(rr);
        Node* a = g->add(text(X0 - 14, yy + h / 2 - 3, p[0].get<std::string>(), style(14, 700, Align::Right), th.text));
        Node* b = g->add(text(X0 - 14, yy + h / 2 + 15, loc.value(v / div, fmt, dec), style(12.5f, 600, Align::Right), th.text2));
        fade(a, kit.t0 + .5f + i * .2f, .3f);
        fade(b, kit.t0 + .5f + i * .2f, .3f);
        yy += h;
        ++i;
    }
    auto ribbon = [](float x0, float a0, float b0, float x1, float a1, float b1) {
        const float xm = (x0 + x1) / 2;
        auto f = [](float v) { return std::to_string(v); };
        return "M" + f(x0) + " " + f(a0) + " C" + f(xm) + " " + f(a0) + "," + f(xm) + " " + f(a1) + "," + f(x1) + " " + f(a1) + " L" + f(x1) + " " +
               f(b1) + " C" + f(xm) + " " + f(b1) + "," + f(xm) + " " + f(b0) + "," + f(x0) + " " + f(b0) + " Z";
    };
    auto reveal = [&](std::unique_ptr<Node> p, float x0, float x1, float t) {
        Node* cl = g->add(group());
        cl->clip = true;
        cl->clipX = x0; cl->clipY = -20; cl->clipW = 0; cl->clipH = r.h + 40;
        cl->clipWidth.fromTo(0, x1 - x0 + 2, t, .85f, ease::p2InOut);
        cl->add(std::move(p));
    };
    const double tmax = tank.value("max", total), tfrom = tank.value("from", 0.0), tto = tank.value("to", 0.0);
    auto lvl = [&](double v) { return Y0 + TH - (float)(v / tmax) * TH; };
    float sy = Y0;
    float hS = 0;
    if (const json& lo = field(c, "leftover"); lo.is_object()) {
        hS = lo["value"].get<float>() * KH;
        const float ty = lvl(tto) + 4;
        reveal(pathNode(ribbon(X0 + XW, sy, sy + hS, TX, ty, ty + hS), th.accent.alpha(.42f)), X0 + XW, TX, TF);
        if (lo.contains("label"))
            fade(g->add(text((X0 + TX) / 2 + 40, sy + hS / 2 - 6, lo["label"].get<std::string>(), style(14, 800, Align::Center), th.text)), TF + .7f, .4f);
        sy += hS;
    }
    float cy = Y0 + hS + 40;
    i = 0;
    for (const auto& o : c.at("outputs")) {
        const double v = o[1].get<double>();
        const std::string tone = o.size() > 2 ? o[2].get<std::string>() : "bad";
        const bool warn = tone == "warn";
        const Color col = kit.tone(tone);
        const float h = std::max(3.f, (float)v * KH);
        const float T = TF + .15f + i * .1f;
        reveal(pathNode(ribbon(X0 + XW, sy, sy + (float)v * KH, X1, cy, cy + h), col.alpha(warn ? .32f : .24f + (i == 0 ? .12f : 0))), X0 + XW, X1, T);
        Node* rr = g->add(rect(X1, cy, XW, h, col));
        grow(rr, T + .6f, true, .35f);
        Node* t = g->add(group());
        const TextStyle a = style(12.5f, 600), b = style(12.5f, 800);
        const std::string lab = o[0].get<std::string>() + "  ";
        t->add(text(X1 + XW + 12, cy + h / 2 + 4, lab, a, th.text2));
        t->add(text(X1 + XW + 12 + kit.measure(lab, a), cy + h / 2 + 4, "\xe2\x88\x92" + loc.nf(v, 0), b, th.text));
        fade(t, T + .75f, .3f);
        sy += (float)v * KH;
        cy += h + GAP;
        ++i;
    }
    if (tank.contains("to")) {
        Node* frame = g->add(group());
        Node* tk = frame->add(rect(TX, Y0, TW, TH, th.text.alpha(.03f), 12));
        tk->stroke = th.line; tk->strokeW = 1.5f;
        const float hTo = Y0 + TH - 3 - lvl(tto), hFrom = Y0 + TH - 3 - lvl(tfrom);
        Node* fl = frame->add(rect(TX + 3, lvl(tto), TW - 6, hTo, th.accent.alpha(.5f), 9));
        fl->ox = 0; fl->oy = hTo;
        if (tank.contains("label"))
            frame->add(text(TX + TW / 2, Y0 - 12, tank["label"].get<std::string>(), style(12, 700, Align::Center, .04f), th.text2));
        fade(frame, kit.t0 + .4f, .5f);
        const float TS = kit.at(field(tank, "at"), 7);
        fl->scaleY.fromTo(hFrom / hTo, 1, TS, 1.4f, ease::p2InOut);
        Node* val = g->add(text(TX + TW / 2, Y0 + TH + 40, "", style(30, 800, Align::Center, -.02f), th.accent));
        val->format = [loc, div, fmt, dec](double v) { return loc.value(v / div, fmt, dec); };
        val->value.fromTo((float)tfrom, (float)tto, TS, 1.4f, ease::p2InOut);
        fade(val, kit.t0 + .5f, .4f);
        if (tank.contains("sub")) fade(g->add(text(TX + TW / 2, Y0 + TH + 62, tank["sub"].get<std::string>(), style(11.5f, 400, Align::Center), th.text2)), TS + 1.3f, .4f);
        if (tank.contains("sub2")) fade(g->add(text(TX + TW / 2, Y0 + TH + 80, tank["sub2"].get<std::string>(), style(11, 400, Align::Center), th.text3)), TS + 1.5f, .4f);
    }
    if (const json& hl = field(c, "highlight"); hl.is_object() && !parts.empty()) {
        const float T = kit.at(field(hl, "at"), 4);
        const std::string s = hl["text"].get<std::string>();
        const TextStyle st = style(12, 700, Align::Center);
        const float tw = kit.measure(s, st) + 24;
        Node* pill = g->add(group());
        Node* bg = pill->add(rect(X0 + XW / 2 - tw / 2, Y0 + 350 + 18, tw, 28, th.accent.alpha(.14f), 14));
        bg->stroke = th.accent.alpha(.45f); bg->strokeW = 1;
        pill->add(text(X0 + XW / 2, Y0 + 350 + 36, s, st, th.accent));
        enter(pill, T, 0, 8);
        pulse(parts[0], T, mix(th.accent, Color{255, 255, 255, 1}, .35f));
    }
}

void calendar(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const float W = std::min(300.f, r.w), H = 456;
    Node* g = parent->add(group(r.x, r.y + 10));
    g->add(rect(0, 0, W, H, th.panel, 14));
    const float bandH = 15 * kNormalLine + 24;
    const std::string bp = "M0 " + std::to_string(bandH) + " L0 14 Q0 0 14 0 L" + std::to_string(W - 14) + " 0 Q" + std::to_string(W) + " 0 " +
                           std::to_string(W) + " 14 L" + std::to_string(W) + " " + std::to_string(bandH) + " Z";
    g->add(pathNode(bp, th.accent));
    label(g, W / 2, 12, upper(c.value("band", "")), style(15, 800, Align::Center, .18f), kit.onTone("accent"));
    float y = bandH + 22;
    label(g, W / 2, y, c.value("day", ""), style(104, 800, Align::Center), th.text, 104);
    y += 104 + 8;
    label(g, W / 2, y, upper(c.value("month", "")), style(13, 700, Align::Center, .2f), th.text2);
    y += 13 * kNormalLine + 24;
    g->add(rect(28, y, W - 56, 1, th.text.alpha(.1f)));
    y += 21;
    // Clock with hands set from the time ("13h30" / "13:30").
    const std::string tm = c.value("time", "");
    int hh = 0, mm = 0;
    if (std::sscanf(tm.c_str(), "%d%*[h:]%d", &hh, &mm) < 1) hh = 0;
    const float ccx = W / 2, ccy = y + 37;
    g->add(arc(ccx, ccy, 33, 0, 360, th.accent, 3));
    for (int i = 0; i < 12; ++i) {
        Node* tick = g->add(group(ccx, ccy));
        tick->rotation.set(i * 30.f, 0);
        tick->add(line(0, -27, 0, -23, th.text3, 2));
    }
    const float TC = kit.at(field(c, "at"), 1);
    Node* hHand = g->add(group(ccx, ccy));
    hHand->add(line(0, 0, 0, -15, th.text, 4))->roundCap = true;
    hHand->rotation.fromTo(0, ((hh % 12) + mm / 60.f) * 30, TC + .3f, 1.3f, ease::p3InOut);
    Node* mHand = g->add(group(ccx, ccy));
    mHand->add(line(0, 0, 0, -24, th.accent, 3))->roundCap = true;
    mHand->rotation.fromTo(0, 360 + mm * 6.f, TC + .3f, 1.3f, ease::p3InOut);
    g->add(arc(ccx, ccy, 1.75f, 0, 360, th.text, 3.5f));
    y += 74 + 10;
    Node* ht = label(g, W / 2, y, tm, style(46, 800, Align::Center), th.accent, 46);
    ht->ox = 0; ht->oy = -16;
    ht->opacity.fromTo(0, 1, TC + 1.2f, .5f, ease::back(2));
    ht->scale.fromTo(.8f, 1, TC + 1.2f, .5f, ease::back(2));
    y += 46 + 8;
    if (c.contains("sub")) label(g, W / 2, y, c["sub"].get<std::string>(), style(12, 400, Align::Center), th.text3);
    g->ox = W / 2; g->oy = H / 2;
    enter(g, TC, 0, 30, .7f, ease::back(1.4f));
}

}  // namespace

Node* chip(Kit& kit, Node* parent, float x, float y, const std::string& s, const std::string& tone, float font, float padX, float padY, float* wOut) {
    const Theme& th = kit.th;
    Color bg = th.good.alpha(.15f), fg = th.goodText;
    if (tone == "neutral") { bg = th.text.alpha(.07f); fg = th.text2; }
    else if (tone == "bad") { bg = th.bad.alpha(.14f); fg = th.bad; }
    else if (tone == "warn") { bg = th.warn.alpha(.15f); fg = th.warn; }
    else if (tone == "accent") { bg = th.accent.alpha(.12f); fg = th.accent; }
    else if (tone == "blue") { bg = th.blue.alpha(.14f); fg = th.blue; }
    const TextStyle st = style(font, 600);
    const float w = kit.measure(s, st) + 2 * padX, h = font * kNormalLine + 2 * padY;
    Node* g = parent->add(group(x, y));
    g->add(rect(0, 0, w, h, bg, 6));
    g->add(text(padX, baseline(padY, font, font * kNormalLine), s, st, fg));
    g->ox = w / 2; g->oy = h / 2;
    if (wOut) *wOut = w;
    return g;
}

bool isAnnotation(const std::string& t) { return t == "threshold" || t == "callout" || t == "bracket" || t == "note"; }

float intrinsicHeight(const Kit& kit, const json& c, float w) {
    const std::string t = c.value("type", "");
    if (t == "number") return c.value("font", 66.f) + 4;
    if (t == "text") return textHeight(kit, c, w);
    if (t == "chips") return c.value("dir", "row") == "column" ? c["items"].size() * (kChipH + 8) : kChipH;
    if (t == "kv") return 24 + (c.contains("title") ? 10.5f * kNormalLine : 0) + c["rows"].size() * (7 + 13 * kNormalLine);
    return -1;
}

void buildComponent(Kit& kit, Node* parent, const json& c, Rect r) {
    const std::string t = c.at("type").get<std::string>();
    if (t == "number") number(kit, parent, c, r);
    else if (t == "text") textBlock(kit, parent, c, r);
    else if (t == "chips") chips(kit, parent, c, r);
    else if (t == "kv") kv(kit, parent, c, r);
    else if (t == "list") list(kit, parent, c, r);
    else if (t == "bars") bars(kit, parent, c, r);
    else if (t == "lines") lines(kit, parent, c, r);
    else if (t == "stackbar") stackbar(kit, parent, c, r);
    else if (t == "ruler") ruler(kit, parent, c, r);
    else if (t == "meter") meter(kit, parent, c, r);
    else if (t == "rings") rings(kit, parent, c, r);
    else if (t == "gauge") gauge(kit, parent, c, r);
    else if (t == "flow") flow(kit, parent, c, r);
    else if (t == "calendar") calendar(kit, parent, c, r);
    else throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": unknown component '" + t + "'");
}

// ---------- annotations ----------

void buildAnnotation(Kit& kit, const json& c) {
    const Theme& th = kit.th;
    const std::string t = c.at("type").get<std::string>();
    const std::string id = c.at("target").get<std::string>();
    auto it = kit.charts.find(id);
    if (it == kit.charts.end())
        throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": " + t + " targets unknown chart '" + id + "' (it must come after the chart, in the same box)");
    ChartInfo& ci = it->second;
    Node* g = ci.layer;
    const Color col = kit.tone(c.value("tone", t == "threshold" ? "blue" : "accent"));

    if (t == "threshold") {
        const double BL = c.at("value").get<double>();
        const float TL = kit.at(field(c, "at"), 4);
        const float yb = ci.y(BL);
        Node* water = g->add(rect(ci.left, yb, ci.right - ci.left, ci.bottom - yb, col.alpha(.13f)));
        if (c.value("style", "water") == "water") {
            water->oy = ci.bottom - yb;
            water->scaleY.fromTo(0, 1, TL, 1.2f, ease::p2Out);
        } else {
            fade(water, TL, .6f);
        }
        Node* ln = g->add(line(ci.left, yb, ci.right, yb, col, 2.5f));
        ln->ty.fromTo(ci.bottom - yb, 0, TL, 1.2f, ease::p2Out);
        if (c.contains("label")) fade(g->add(text(ci.left + 10, yb - 9, c["label"].get<std::string>(), style(13, 800), col)), TL + 1.1f, .4f);
        // Value labels that would sit right on the line move inside the bar.
        for (size_t i = 0; i < ci.labels.size(); ++i) {
            Node* lb = ci.labels[i];
            if (!lb) continue;
            if (lb->y > yb - 4 && lb->y < yb + 16 && ci.values[i] < BL) {
                lb->y = ci.y(ci.values[i]) + 17;
                // Dark text on a filled bar; hatched (projected) bars keep the regular text color.
                lb->fill = ci.projection[i] ? th.text : mix(th.warn, Color{0, 0, 0, 1}, .82f);
            }
        }
        if (const json& rc = field(c, "recolor"); rc.is_object()) {
            const Color up = kit.tone(rc.value("above", "accent")), down = mix(kit.tone(rc.value("below", "warn")), th.panel, .25f);
            const Color upS = kit.tone(rc.value("above", "accent")), downS = kit.tone(rc.value("below", "warn"));
            for (size_t i = 0; i < ci.bars.size(); ++i) {
                const bool above = ci.values[i] >= BL;
                const float tt = TL + 1.25f + i * .04f;
                Node* b = ci.bars[i];
                if (ci.projection[i]) b->strokeColor.to(above ? upS : downS, tt, .5f, ease::p1Out, b->stroke);
                else b->color.to(above ? up : down, tt, .5f, ease::p1Out, b->fill);
            }
        }
    } else if (t == "callout") {
        const int i = c.at("idx").get<int>();
        const float T = kit.at(field(c, "at"), 3);
        const std::string s = c.at("text").get<std::string>();
        const std::string tone = c.value("tone", "accent");
        double v;
        if (c.contains("value")) v = c["value"].get<double>();
        else if (c.contains("series")) v = ci.series.at(c["series"].get<std::string>())[i];
        else v = i < (int)ci.tops.size() ? ci.tops[i] : ci.values[i];
        const float x = ci.x(i), yv = ci.y(v);
        if (c.contains("series")) {
            if (auto p = ci.seriesPaths.find(c["series"].get<std::string>()); p != ci.seriesPaths.end()) p->second->strokeW = 3.4f;
        }
        Node* box = g->add(group());
        if (c.value("shape", "pill") == "diamond") {
            box->add(pathNode("M" + std::to_string(x) + " " + std::to_string(yv - 7) + " L" + std::to_string(x + 7) + " " + std::to_string(yv) + " L" +
                                  std::to_string(x) + " " + std::to_string(yv + 7) + " L" + std::to_string(x - 7) + " " + std::to_string(yv) + " Z",
                              col));
            box->add(text(x, yv - 14, s, style(11.5f, 800, Align::Center), col));
            enter(box, T, 0, -6);
        } else if (c.value("place", "above") == "left") {
            const TextStyle st = style(13, 800, Align::Center);
            const float tw = kit.measure(s, st) + 26;
            box->add(rect(x - tw - 14, yv - 46, tw, 30, col, 8));
            box->add(text(x - 14 - tw / 2, yv - 26, s, st, th.bg));
            enter(box, T + .2f, 8, 0);
        } else {
            const TextStyle st = style(12, 800, Align::Center);
            const float tw = kit.measure(s, st) + 22;
            box->add(rect(x - tw / 2, yv - 58, tw, 26, col, 6));
            box->add(text(x, yv - 40, s, st, kit.onTone(tone)));
            enter(box, T, 0, 8);
        }
        if (c.value("ping", false)) {
            // A ring that grows from radius 6 to 30 and fades, twice; the stroke keeps its width.
            auto ring = std::make_unique<Node>();
            ring->kind = NodeKind::Circle;
            ring->x = x; ring->y = yv; ring->radius = 6;
            ring->stroke = col; ring->strokeW = 3;
            ring->opacity.set(0, 0);
            for (int rep = 0; rep < 2; ++rep) {
                ring->value.fromTo(6, 30, T + rep * 1.1f, 1.1f, ease::p2Out);
                ring->opacity.fromTo(1, 0, T + rep * 1.1f, 1.1f, ease::p2Out);
            }
            g->add(std::move(ring));
        }
        if (c.value("pulse", false) && i < (int)ci.bars.size())
            pulse(ci.bars[i], T, mix(ci.bars[i]->color.empty() ? ci.bars[i]->fill : ci.bars[i]->color.eval(T), Color{255, 255, 255, 1}, .3f), 2, .2f);
    } else if (t == "bracket") {
        const int a = c.at("from").get<int>(), b = c.at("to").get<int>();
        const float T = kit.at(field(c, "at"), 2);
        float xa, xb, top;
        if (c.value("place", "top") == "bars") {
            xa = ci.x(a) - ci.slot * .38f;
            xb = ci.x(b) + ci.slot * .38f;
            double hi = -1e300;
            for (int i = a; i <= b && i < (int)ci.tops.size(); ++i) hi = std::max(hi, ci.tops[i]);
            top = ci.y(hi) - 46;
        } else {
            xa = ci.x(a) - ci.slot * .35f;
            xb = ci.x(b) + ci.slot * .35f;
            top = 46;
        }
        Node* br = g->add(group());
        if (c.value("shade", false)) br->add(rect(xa, top, xb - xa, ci.bottom - top, col.alpha(.06f)));
        br->add(pathNode("M" + std::to_string(xa) + " " + std::to_string(top + 14) + " L" + std::to_string(xa) + " " + std::to_string(top) + " L" +
                             std::to_string(xb) + " " + std::to_string(top) + " L" + std::to_string(xb) + " " + std::to_string(top + 14),
                         Color{}, col, 2));
        br->add(text((xa + xb) / 2, top - 9, c.at("text").get<std::string>(), style(12.5f, 800, Align::Center), col));
        enter(br, T, 0, 6, .45f);
    } else if (t == "note") {
        const int i = c.at("idx").get<int>();
        const float T = kit.at(field(c, "at"), 9);
        Node* box = g->add(group());
        const float x0 = ci.x(i) + ci.slot * .3f + 14;
        int j = 0;
        for (const auto& l : c.at("lines")) {
            box->add(text(x0, ci.top + 18 + j * 18, l.get<std::string>(), style(j == 0 ? 13 : 11.5f, j == 0 ? 800 : 600), j == 0 ? col : th.text2));
            ++j;
        }
        enter(box, T + .3f, -8, 0, .45f);
    }
}
