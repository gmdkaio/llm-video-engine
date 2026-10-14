// Page components of plan@1 (see skill/COMPONENTS.md): number, text, chips, kv, list, kpi, table.
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

// ---------- kpi card ----------

// Number format shared by kpi and table: format, decimals, divide, prefix, suffix, sign (+ on positives).
std::string fmtValue(const Locale& loc, const json& f, double v) {
    const double d = f.value("divide", 1.0);
    std::string s = loc.value(v / (d == 0 ? 1 : d), f.value("format", "num"), f.value("decimals", -1), f.value("prefix", ""), f.value("suffix", ""));
    if (f.value("sign", false) && v > 0) s = "+" + s;
    return s;
}

struct KpiLayout { float font, labelH, valueH, deltaH, subH, sparkH; };
KpiLayout kpiLayout(const json& c, float w, float h) {
    KpiLayout L;
    const float autoFont = h > 0 ? std::clamp(std::min(h * .3f, w * .17f), 22.f, 48.f) : 40.f;
    L.font = c.value("font", autoFont);
    L.labelH = c.contains("label") ? 10.5f * kNormalLine + 8 : 0;
    L.valueH = L.font * 1.08f;
    L.deltaH = c.contains("delta") ? 10 + 12 * kNormalLine + 8 : 0;
    L.subH = c.contains("sub") ? 6 + 12 * kNormalLine : 0;
    L.sparkH = c.contains("spark") ? 44.f : 0.f;
    return L;
}
float kpiHeight(const json& c, float w) {
    const KpiLayout L = kpiLayout(c, w, -1);
    return 36 + L.labelH + L.valueH + L.deltaH + L.subH + L.sparkH;
}

void kpi(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const float pad = 18;
    const KpiLayout L = kpiLayout(c, r.w, r.h);
    Node* g = parent->add(group(r.x, r.y));
    const float t = kit.at(field(c, "at"), 1);
    if (c.value("card", true)) {
        Node* bg = g->add(rect(0, 0, r.w, r.h, th.panel, 12));
        if (c.value("highlight", false)) { bg->stroke = th.accent.alpha(.6f); bg->strokeW = 1.5f; }
    }
    // Content is vertically centered in the card, the sparkline sits at the bottom.
    const float content = L.labelH + L.valueH + L.deltaH + L.subH;
    float y = std::max(pad, (r.h - L.sparkH - content) / 2);
    if (c.contains("label")) {
        label(g, pad, y, upper(str(c, "label")), style(10.5f, 700, Align::Left, .1f), th.text2);
        y += L.labelH;
    }
    TextStyle vs = style(L.font, 800, Align::Left, -.02f);
    vs.tnum = true;
    const Color vc = kit.tone(c.value("tone", "text"));
    if (c.contains("value") && c["value"].is_number()) {
        // Runs at render time, after the Kit is gone: capture by value.
        const Locale loc = kit.loc;
        const json f = c;
        Node* n = g->add(text(pad, baseline(y, L.font, L.valueH), "", vs, vc));
        n->format = [loc, f](double v) { return fmtValue(loc, f, v); };
        n->value.fromTo((float)c.value("from", 0.0), (float)c["value"].get<double>(), t + .15f, c.value("dur", 1.1f),
                        c.contains("from") ? ease::p3InOut : ease::p2Out);
    } else {
        g->add(text(pad, baseline(y, L.font, L.valueH), str(c, "text"), vs, vc));
    }
    y += L.valueH;
    if (c.contains("delta")) {
        const json& d = c["delta"];
        const bool num = d.contains("value") && d["value"].is_number();
        const double dv = num ? d["value"].get<double>() : 0;
        json df = d;
        if (!df.contains("sign")) df["sign"] = true;
        const std::string s = num ? fmtValue(kit.loc, df, dv) : str(d, "text");
        const std::string tn = d.value("tone", dv < 0 ? "bad" : "good");
        const Color col = kit.tone(tn);
        const TextStyle ds = style(12, 700);
        const bool arrow = num && dv != 0 && d.value("arrow", true);
        const float aw = arrow ? 12.f : 0.f, w = kit.measure(s, ds) + 20 + aw, h = 12 * kNormalLine + 8;
        Node* pill = g->add(group(pad, y + 10));
        pill->add(rect(0, 0, w, h, col.alpha(.15f), 6));
        if (arrow) {
            const float cx = 14, cy = h / 2;
            pill->add(pathNode(dv > 0 ? "M" + std::to_string(cx - 4) + " " + std::to_string(cy + 2.5f) + " L" + std::to_string(cx + 4) + " " + std::to_string(cy + 2.5f) + " L" + std::to_string(cx) + " " + std::to_string(cy - 3.5f) + " Z"
                                      : "M" + std::to_string(cx - 4) + " " + std::to_string(cy - 2.5f) + " L" + std::to_string(cx + 4) + " " + std::to_string(cy - 2.5f) + " L" + std::to_string(cx) + " " + std::to_string(cy + 3.5f) + " Z",
                               col));
        }
        pill->add(text(10 + aw, baseline(4, 12, 12 * kNormalLine), s, ds, col));
        if (d.contains("label")) pill->add(text(w + 8, baseline(4, 12, 12 * kNormalLine), str(d, "label"), style(12, 500), th.text3));
        pill->ox = w / 2; pill->oy = h / 2;
        pop(pill, t + .9f, 6, .9f);
        y += L.deltaH;
    }
    if (c.contains("sub")) {
        fade(label(g, pad, y + 6, str(c, "sub"), style(12, 500), th.text3), t + 1.1f, .4f);
    }
    if (c.contains("spark") && c["spark"].size() >= 2) {
        const auto& sp = c["spark"];
        double lo = 1e300, hi = -1e300;
        for (const auto& v : sp) { lo = std::min(lo, v.get<double>()); hi = std::max(hi, v.get<double>()); }
        if (hi <= lo) hi = lo + 1;
        const float x0 = pad, x1 = r.w - pad, yb = r.h - 12, yt = r.h - L.sparkH + 6;
        std::string p;
        for (size_t i = 0; i < sp.size(); ++i) {
            const float x = x0 + (x1 - x0) * i / (sp.size() - 1), yy = yb - (yb - yt) * (float)((sp[i].get<double>() - lo) / (hi - lo));
            p += (i ? "L" : "M") + std::to_string(x) + " " + std::to_string(yy) + " ";
        }
        Node* ln = g->add(pathNode(p, Color{}, kit.tone(c.value("spark_tone", "accent")), 2));
        ln->trim.fromTo(0, 1, t + .3f, 1.2f, ease::p2InOut);
    }
    g->ox = r.w / 2; g->oy = r.h / 2;
    pop(g, t, 12, .97f);
}

// ---------- table ----------

void table(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    const auto& cols = field(c, "columns");
    const auto& rows = field(c, "rows");
    const size_t nc = cols.size(), nr = rows.size();
    if (nc == 0) throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": table without columns");
    const bool header = c.value("header", true);
    const float rh = std::min(c.value("row_height", 40.f), r.h / (float)(nr + (header ? 1 : 0)));
    const float font = c.value("font", std::min(13.5f, rh * .42f));
    const std::string neg = c.value("negative", "");

    // Cell texts first: the first column is as wide as its longest label, the rest share what is left.
    std::vector<std::vector<std::string>> cell(nr, std::vector<std::string>(nc));
    std::vector<std::vector<bool>> negative(nr, std::vector<bool>(nc, false));
    for (size_t i = 0; i < nr; ++i) {
        const auto& cs = rows[i].is_array() ? rows[i] : field(rows[i], "cells");
        for (size_t j = 0; j < nc && j < cs.size(); ++j) {
            if (cs[j].is_number()) { cell[i][j] = fmtValue(kit.loc, cols[j], cs[j].get<double>()); negative[i][j] = cs[j].get<double>() < 0; }
            else if (cs[j].is_string()) cell[i][j] = cs[j].get<std::string>();
        }
    }
    const float indentW = 16;
    float w0 = 0;
    for (size_t i = 0; i < nr; ++i) {
        const json& R = rows[i].is_object() ? rows[i] : kNull;
        const bool bold = R.is_object() && (R.value("bold", false) || R.value("total", false));
        w0 = std::max(w0, kit.measure(cell[i][0], style(font, bold ? 700 : 500)) + (R.is_object() ? R.value("indent", 0) : 0) * indentW);
    }
    if (header) w0 = std::max(w0, kit.measure(upper(str(cols[0], "label")), style(10, 700, Align::Left, .08f)));
    w0 = std::min(w0 + 24, r.w * .5f);
    if (cols[0].contains("width")) w0 = cols[0]["width"].get<float>();
    double share = 0;
    for (size_t j = 1; j < nc; ++j) share += cols[j].value("width", 1.0);
    std::vector<float> cx(nc + 1, 0);  // column left edges
    cx[1] = w0;
    for (size_t j = 1; j < nc; ++j) cx[j + 1] = cx[j] + (float)((r.w - w0) * cols[j].value("width", 1.0) / std::max(share, 1e-9));
    auto align = [&](size_t j) { return str(cols[j], "align", j == 0 ? "left" : "right"); };
    auto anchorX = [&](size_t j, const std::string& a) { return a == "left" ? cx[j] + (j ? 10.f : 0.f) : a == "center" ? (cx[j] + cx[j + 1]) / 2 : cx[j + 1] - 10; };

    Node* g = parent->add(group(r.x, r.y));
    const float t0 = kit.at(field(c, "at"), 1);
    const float stagger = c.value("stagger", std::min(.12f, 2.4f / std::max<size_t>(1, nr)));
    float y = 0;
    // Column bands for changes that highlight a column; behind everything.
    std::vector<Node*> colBand(nc, nullptr);
    for (const auto& ch : field(c, "changes"))
        if (ch.contains("cols") && !ch.contains("rows") && ch.value("highlight", false))
            for (const auto& j : ch["cols"])
                if (j.get<size_t>() < nc && !colBand[j.get<size_t>()]) {
                    const size_t k = j.get<size_t>();
                    colBand[k] = g->add(rect(cx[k] + 2, 0, cx[k + 1] - cx[k] - 4, rh * (nr + (header ? 1 : 0)), th.accent.alpha(.12f), 6));
                    colBand[k]->opacity.set(0, 0);
                }
    if (header) {
        Node* hg = g->add(group());
        for (size_t j = 0; j < nc; ++j) {
            const std::string a = align(j);
            hg->add(text(anchorX(j, a), baseline(0, 10, rh), upper(str(cols[j], "label")),
                         style(10, 700, a == "left" ? Align::Left : a == "center" ? Align::Center : Align::Right, .08f), th.text3));
        }
        hg->add(line(0, rh - .5f, r.w, rh - .5f, th.line, 1));
        fade(hg, t0 - .1f, .4f);
        y = rh;
    }
    struct RowNodes { Node* g; Node* bg; std::vector<Node*> texts; };
    std::vector<RowNodes> rn;
    for (size_t i = 0; i < nr; ++i) {
        const json& R = rows[i].is_object() ? rows[i] : kNull;
        const bool total = R.is_object() && R.value("total", false);
        const bool bold = total || (R.is_object() && R.value("bold", false));
        const int indent = R.is_object() ? R.value("indent", 0) : 0;
        Node* rg = g->add(group(0, y));
        Node* bg = rg->add(rect(0, 1, r.w, rh - 2, th.accent.alpha(.14f), 6));
        if (!(R.is_object() && R.value("highlight", false))) bg->opacity.set(0, 0);
        if (total) rg->add(line(0, .5f, r.w, .5f, th.line, 1));
        else if (c.value("lines", false)) rg->add(line(0, rh - .5f, r.w, rh - .5f, th.grid, 1));
        RowNodes nodes{rg, bg, {}};
        const Color rowTone = kit.tone(R.is_object() ? R.value("tone", bold ? "text" : "text2") : "text2");
        for (size_t j = 0; j < nc; ++j) {
            const std::string a = align(j);
            TextStyle st = style(font, bold ? 700 : (j == 0 ? 500 : 600), a == "left" ? Align::Left : a == "center" ? Align::Center : Align::Right);
            st.tnum = true;
            Color col = j == 0 ? rowTone : (bold ? th.text : th.text2);
            if (R.is_object() && R.contains("tone") && j > 0) col = rowTone;
            if (cols[j].contains("tone")) col = kit.tone(cols[j]["tone"].get<std::string>());
            if (negative[i][j] && !neg.empty()) col = kit.tone(neg);
            const float x = anchorX(j, a) + (j == 0 ? indent * indentW + 8 : 0);
            nodes.texts.push_back(rg->add(text(x, baseline(0, font, rh), cell[i][j], st, col)));
        }
        const float t = R.is_object() && R.contains("at") ? kit.at(R["at"], t0) : t0 + i * stagger;
        enter(rg, t, 0, 8, .4f);
        rn.push_back(nodes);
        y += rh;
    }
    // changes: [{at, rows, cols, highlight, tone, opacity, pulse}]
    for (const auto& ch : field(c, "changes")) {
        const float t = kit.at(field(ch, "at"), 4);
        std::vector<size_t> rs, cs;
        for (const auto& v : field(ch, "rows")) rs.push_back(v.get<size_t>());
        for (const auto& v : field(ch, "cols")) cs.push_back(v.get<size_t>());
        if (rs.empty() && !cs.empty() && ch.value("highlight", false)) {
            for (size_t k : cs) if (k < nc && colBand[k]) colBand[k]->opacity.to(1, t, .4f, ease::p1Out, 0);
            continue;
        }
        if (rs.empty()) for (size_t i = 0; i < nr; ++i) rs.push_back(i);
        if (ch.value("invert", false)) {
            std::vector<size_t> all;
            for (size_t i = 0; i < nr; ++i) if (std::find(rs.begin(), rs.end(), i) == rs.end()) all.push_back(i);
            rs = all;
        }
        for (size_t i : rs) {
            if (i >= nr) continue;
            if (ch.value("highlight", false)) rn[i].bg->opacity.to(1, t, .4f, ease::p1Out, 0);
            if (ch.contains("opacity")) rn[i].g->opacity.to(ch["opacity"].get<float>(), t, .4f, ease::p1Out, 1);
            for (size_t j = 0; j < nc; ++j) {
                if (!cs.empty() && std::find(cs.begin(), cs.end(), j) == cs.end()) continue;
                Node* n = rn[i].texts[j];
                if (ch.contains("tone")) n->color.to(kit.tone(ch["tone"].get<std::string>()), t, .4f, ease::p1Out, n->fill);
                if (ch.value("pulse", false)) pulse(n, t, th.accent, 2, .2f);
            }
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
    if (t == "kpi") return kpiHeight(c, w);
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
    else if (t == "kpi") kpi(kit, parent, c, r);
    else if (t == "table") table(kit, parent, c, r);
    else if (t == "chart") chart(kit, parent, c, r);
    else throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": unknown component '" + t + "'");
}

// ---------- annotations ----------

