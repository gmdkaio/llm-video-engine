// Page components of plan@1 (see skill/COMPONENTS.md): number, text, chips, kv, list.
// Charts are built from marks in chart.cpp.
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
    else if (t == "chart") chart(kit, parent, c, r);
    else throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": unknown component '" + t + "'");
}

// ---------- annotations ----------

