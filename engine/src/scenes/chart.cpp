// The `chart` component: any chart a deck shows, described as layers of marks
// (bar, line, area, rect, rule, point, text, arc, ribbon, path) on shared scales.
// No chart types are built in; a waterfall, a combo or a donut is just data and
// marks, so new decks need no new code. See skill/COMPONENTS.md for the format.
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

#include "scenes/kit.h"

namespace {

const json kNull;
const json& f(const json& o, const char* k) {
    auto it = o.find(k);
    return it == o.end() ? kNull : *it;
}
double num(const json& o, const char* k, double d) {
    const json& v = f(o, k);
    return v.is_number() ? v.get<double>() : d;
}
std::string s(const json& o, const char* k, const std::string& d = "") {
    const json& v = f(o, k);
    return v.is_string() ? v.get<std::string>() : d;
}
std::string fs(float v) { return std::to_string(v); }

double niceStep(double span) {
    const double m = span * 1.08;
    const double p = std::pow(10.0, std::floor(std::log10(m > 0 ? m : 1)));
    for (double x : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m / (x * p) <= 5) return x * p;
    return 10 * p;
}

// One axis: band (categories) or linear, mapped to screen positions p0..p1.
struct Axis {
    bool band = false;
    std::vector<std::string> cats;
    double min = 0, max = 1, step = 1;
    float p0 = 0, p1 = 1;
    int index(const json& x) const {
        if (x.is_number()) return x.get<int>();
        const std::string v = x.is_string() ? x.get<std::string>() : "";
        for (size_t i = 0; i < cats.size(); ++i)
            if (cats[i] == v) return (int)i;
        return 0;
    }
    float slot() const { return cats.empty() ? 0 : (p1 - p0) / cats.size(); }
    float lin(double v) const { return p0 + (float)((v - min) / (max - min)) * (p1 - p0); }
    float at(const json& x) const {
        if (band) return p0 + slot() * (index(x) + .5f);
        return lin(x.is_number() ? x.get<double>() : 0);
    }
    float edge0(const json& x) const { return band ? p0 + slot() * index(x) : at(x); }
    float edge1(const json& x) const { return band ? p0 + slot() * (index(x) + 1) : at(x); }
};

struct Fmt {
    std::string format, prefix, suffix;
    double divide = 1;
    int decimals = -1;
    bool sign = false;
    static Fmt from(const json& o) {
        Fmt r;
        if (!o.is_object()) return r;
        r.format = s(o, "format");
        r.prefix = s(o, "prefix");
        r.suffix = s(o, "suffix");
        r.divide = num(o, "divide", 1);
        r.decimals = (int)num(o, "decimals", -1);
        r.sign = f(o, "sign").is_boolean() && f(o, "sign").get<bool>();
        return r;
    }
    std::string operator()(const Locale& loc, double v) const {
        const double x = v / divide;
        std::string out;
        if (!format.empty()) out = loc.value(x, format, decimals, prefix, suffix);
        else out = prefix + (x < 0 ? "\xe2\x88\x92" : "") + loc.nf(std::fabs(x), decimals >= 0 ? decimals : (std::fabs(x) < 10 && x != (long long)x ? 1 : 0)) + suffix;
        if (sign && v > 0) out = "+" + out;
        return out;
    }
};

struct Ctx {
    Kit& kit;
    const json& c;
    Rect plot;               // in the chart group's coordinates
    bool horizontal = false;
    bool polar = false;
    Axis X, Y;               // datum.x and datum.y
    Axis Y2;                 // second value scale, for layers with axis: "y2"
    double base = 0, base2 = 0;  // bar baseline in y units
    // Polar: x is the angle (a category, or a value on pmin..pmax), y the radius (rmin..rmax).
    float cx = 0, cy = 0, R = 0;
    double pmin = 0, pmax = 1, pstart = 0, pend = 360, rmin = 0, rmax = 1;
    std::vector<std::string> pcats;
    float catAngle(size_t i) const {
        const bool full = std::fabs(pend - pstart) >= 359.5;
        const size_t n = full ? pcats.size() : std::max<size_t>(1, pcats.size() - 1);
        return (float)(pstart + (pend - pstart) * i / std::max<size_t>(1, n));
    }
    float thetaOf(const json& x) const {
        if (x.is_string())
            for (size_t i = 0; i < pcats.size(); ++i) if (pcats[i] == x.get<std::string>()) return catAngle(i);
        return ang(x.is_number() ? x.get<double>() : pmin);
    }
    std::pair<float, float> pt(const json& x, double y) const {
        if (polar) {
            const float an = thetaOf(x) * (float)M_PI / 180, rr = R * (float)((y - rmin) / (rmax - rmin));
            return {cx + rr * std::sin(an), cy - rr * std::cos(an)};
        }
        return horizontal ? std::pair<float, float>{Y.lin(y), X.at(x)} : std::pair<float, float>{X.at(x), Y.lin(y)};
    }
    float ang(double v) const { return (float)(pstart + (v - pmin) / (pmax - pmin) * (pend - pstart)); }
};

std::vector<json> dataOf(const json& layer) {
    std::vector<json> out;
    for (const auto& d : f(layer, "data")) out.push_back(d);
    return out;
}

// Generic data transforms: stack, waterfall, cumulative. They fill y0/y1 (or y).
void transform(const json& layer, std::vector<json>& data, std::map<std::string, std::pair<double, double>>& stacks) {
    const std::string t = s(layer, "transform");
    for (auto& d : data)
        if (d.contains("y") && d["y"].is_number()) d["_v"] = d["y"];
    if (t == "cumulative") {
        double run = 0;
        for (auto& d : data)
            if (d.contains("y") && d["y"].is_number()) { run += d["y"].get<double>(); d["y"] = run; d["_v"] = run; }
    } else if (t == "waterfall") {
        double run = 0;
        for (auto& d : data) {
            const bool total = d.value("total", false);
            const double v = num(d, "y", 0);
            if (total) { d["y0"] = 0.0; d["y1"] = d.contains("y") ? v : run; run = d["y1"].get<double>(); d["_v"] = run; }
            else { d["y0"] = run; d["y1"] = run + v; run += v; }
        }
    } else if (t == "stack") {
        for (auto& d : data) {
            const std::string key = d.contains("x") ? d["x"].dump() : "";
            const double v = num(d, "y", 0);
            auto& [pos, neg] = stacks[key];
            if (v >= 0) { d["y0"] = pos; d["y1"] = pos + v; pos += v; }
            else { d["y0"] = neg; d["y1"] = neg + v; neg += v; }
        }
    }
}

struct Anim {
    std::string enter;
    float at0;
    float stagger, dur;
};

float datumTime(Ctx& cx, const json& d, const Anim& a, int i) {
    if (d.contains("at")) return cx.kit.at(d["at"], 1);
    return a.at0 + i * a.stagger;
}

Color toneOf(Ctx& cx, const json& layer, const json& d, const std::string& dflt) {
    return cx.kit.tone(s(d, "tone", s(layer, "tone", dflt)));
}

// Value label next to a mark.
Node* valueLabel(Ctx& cx, Node* g, const json& L, const json& d, double v, float x, float y, float t, Align a = Align::Center) {
    const Kit& kit = cx.kit;
    const Fmt fm = Fmt::from(L);
    const std::string str = d.contains("label") && d["label"].is_string() ? d["label"].get<std::string>() : fm(kit.loc, v);
    const float size = (float)num(L, "size", 11);
    Node* n = g->add(text(x, y, str, style(size, (int)num(L, "weight", 700), a), cx.kit.tone(s(d, "label_tone", s(L, "tone", "text2")))));
    fade(n, t, .3f);
    return n;
}

bool labelWanted(const json& L, int i, int n) {
    if (!L.is_object() && !(L.is_boolean() && L.get<bool>())) return false;
    const json& only = f(L, "only");
    if (only.is_string()) return only == "last" ? i == n - 1 : only == "first" ? i == 0 : true;
    if (only.is_array()) {
        for (const auto& k : only) if (k.is_number() && k.get<int>() == i) return true;
        return false;
    }
    return true;
}

// Applies `changes` to the datum nodes of a layer: recolor, dim, pulse at a time.
void applyChanges(Ctx& cx, const json& layer, std::vector<Node*>& shapes, std::vector<Node*>& groups, bool strokeColored) {
    for (const auto& ch : f(layer, "changes")) {
        const float t = cx.kit.at(f(ch, "at"), 4);
        std::vector<bool> sel(shapes.size(), false);
        const json& idx = f(ch, "idx");
        if (idx.is_array()) { for (const auto& k : idx) if (k.is_number() && k.get<size_t>() < sel.size()) sel[k.get<size_t>()] = true; }
        else std::fill(sel.begin(), sel.end(), true);
        if (ch.value("invert", false)) for (size_t i = 0; i < sel.size(); ++i) sel[i] = !sel[i];
        for (size_t i = 0; i < shapes.size(); ++i) {
            if (!sel[i]) continue;
            Node* n = shapes[i];
            if (ch.contains("tone")) {
                const Color c = cx.kit.tone(ch["tone"].get<std::string>());
                if (strokeColored) n->strokeColor.to(c, t, .5f, ease::p1Out, n->stroke);
                else n->color.to(c, t, .5f, ease::p1Out, n->fill);
            }
            if (ch.contains("opacity")) groups[i]->opacity.to((float)ch["opacity"].get<double>(), t, .4f, ease::p1Out, 1);
            if (ch.value("pulse", false)) {
                const Color c0 = n->color.empty() ? n->fill : n->color.eval(t);
                pulse(n, t, mix(c0, Color{255, 255, 255, 1}, .3f), 2, .2f);
            }
        }
    }
}

// ---------- marks ----------

void bars(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    const float slot = std::fabs(cx.X.slot());
    const float size = (float)num(L, "size", .6);
    float thick = slot * size, off = 0;
    if (const json& dg = f(L, "dodge"); dg.is_array() && dg.size() == 2) {
        const int i = dg[0].get<int>(), n = std::max(1, dg[1].get<int>());
        thick = slot * size / n;
        off = -slot * size / 2 + thick * (i + .5f);
    }
    const json& lab = f(L, "labels");
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        const double y0 = d.contains("y0") ? d["y0"].get<double>() : cx.base;
        const double y1 = d.contains("y1") ? d["y1"].get<double>() : num(d, "y", cx.base);
        const float c = cx.X.at(f(d, "x")) + off;
        const float v0 = cx.Y.lin(y0), v1 = cx.Y.lin(y1);
        Node* g = lg->add(group());
        Color col = toneOf(cx, L, d, "accent");
        Node* r;
        if (cx.horizontal) r = g->add(rect(std::min(v0, v1), c - thick / 2, std::max(.5f, std::fabs(v1 - v0)), thick, col, (float)num(L, "radius", 3)));
        else r = g->add(rect(c - thick / 2, std::min(v0, v1), thick, std::max(.5f, std::fabs(v1 - v0)), col, (float)num(L, "radius", 3)));
        if (s(d, "pattern", s(L, "pattern")) == "hatch") {
            r->shader = hatchShader(col.alpha(.18f), col.alpha(.8f));
            r->stroke = col; r->strokeW = 1.5f;
        }
        if (d.contains("opacity") || L.contains("opacity")) r->fill = r->fill.alpha((float)num(d, "opacity", num(L, "opacity", 1)));
        const float t = datumTime(cx, d, a, i);
        const bool up = cx.horizontal ? v1 >= v0 : v1 <= v0;  // grows away from y0
        if (a.enter == "grow") {
            r->ox = r->w / 2; r->oy = r->h / 2;
            if (cx.horizontal) { r->ox = up ? 0 : r->w; r->scaleX.fromTo(0, 1, t, a.dur, ease::p3Out); }
            else { r->oy = up ? r->h : 0; r->scaleY.fromTo(0, 1, t, a.dur, ease::p3Out); }
        } else if (a.enter == "fade") fade(g, t, a.dur);
        else if (a.enter == "pop") { g->ox = c; g->oy = (v0 + v1) / 2; pop(g, t); }
        if (labelWanted(lab, i, (int)data.size())) {
            const std::string pos = lab.is_object() ? s(lab, "position", "end") : "end";
            const double v = d.contains("_v") ? d["_v"].get<double>() : y1 - y0;
            const float lt = t + a.dur * .75f;
            if (cx.horizontal) {
                const float x = pos == "inside" ? v1 + (up ? -6 : 6) : pos == "center" ? (v0 + v1) / 2 : v1 + (up ? 6 : -6);
                const Align al = pos == "center" ? Align::Center : ((pos == "inside") == up ? Align::Right : Align::Left);
                valueLabel(cx, g, lab.is_object() ? lab : json::object(), d, v, x, c + 4, lt, al);
            } else {
                const float y = pos == "inside" ? v1 + (up ? 16 : -8) : pos == "center" ? (v0 + v1) / 2 + 4 : v1 + (up ? -7 : 15);
                valueLabel(cx, g, lab.is_object() ? lab : json::object(), d, v, c, y, lt);
            }
        }
        shapes.push_back(r);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, false);
}

std::string polyPath(Ctx& cx, const std::vector<json>& data, bool& any) {
    std::string p;
    bool pen = false;
    any = false;
    for (const auto& d : data) {
        if (!d.contains("y") || !d["y"].is_number()) { pen = false; continue; }
        auto [x, y] = cx.pt(f(d, "x"), d["y"].get<double>());
        p += (pen ? "L" : "M") + fs(x) + " " + fs(y) + " ";
        pen = true;
        any = true;
    }
    return p;
}

// Left-to-right reveal for marks that cannot be trimmed (areas, ribbons).
Node* revealGroup(Ctx& cx, Node* lg, float t, float dur) {
    Node* g = lg->add(group());
    g->clip = true;
    g->clipX = cx.plot.x - 4; g->clipY = cx.plot.y - 40; g->clipH = cx.plot.h + 80; g->clipW = cx.plot.w + 8;
    g->clipWidth.fromTo(0, cx.plot.w + 8, t, dur, ease::p2InOut);
    return g;
}

void lineMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a, bool area) {
    bool any;
    std::string p = polyPath(cx, data, any);
    if (!any) return;
    const Color col = cx.kit.tone(s(L, "tone", "accent"));
    Node* holder = lg;
    if (area && cx.polar) {
        // A polar area is the closed shape itself (radar); it grows from the center.
        Node* g = lg->add(group());
        g->ox = cx.cx; g->oy = cx.cy;
        g->add(pathNode(p + "Z", col.alpha((float)num(L, "opacity", .22))));
        if (a.enter == "grow" || a.enter == "draw" || a.enter == "pop") { g->scale.fromTo(0, 1, a.at0, a.dur, ease::p3Out); fade(g, a.at0, .3f); }
        else if (a.enter != "none") fade(g, a.at0, a.dur);
        return;
    }
    if (L.value("closed", cx.polar && std::fabs(cx.pend - cx.pstart) >= 359.5)) p += "Z";
    if (area) {
        // Close back along y0 (or the baseline).
        std::string back;
        for (auto it = data.rbegin(); it != data.rend(); ++it) {
            if (!it->contains("y")) continue;
            auto [x, y] = cx.pt(f(*it, "x"), num(*it, "y0", cx.base));
            back += "L" + fs(x) + " " + fs(y) + " ";
        }
        p += back + "Z";
        if (a.enter == "grow" || a.enter == "draw") holder = revealGroup(cx, lg, a.at0, a.dur);
        Node* n = holder->add(pathNode(p, col.alpha((float)num(L, "opacity", .22))));
        if (a.enter == "fade") fade(n, a.at0, a.dur);
        return;
    }
    Node* n = lg->add(pathNode(p, Color{}, col, (float)num(L, "width", 2.4)));
    if (const json& dash = f(L, "dash"); dash.is_array()) for (const auto& v : dash) n->dash.push_back(v.get<float>());
    if (a.enter == "grow" || a.enter == "draw") {
        if (!n->dash.empty()) {  // dashes and trim do not mix; reveal instead
            lg->children.pop_back();
            Node* g = revealGroup(cx, lg, a.at0, a.dur);
            n = g->add(pathNode(p, Color{}, col, (float)num(L, "width", 2.4)));
            for (const auto& v : f(L, "dash")) n->dash.push_back(v.get<float>());
        } else n->trim.fromTo(0, 1, a.at0, a.dur, ease::p2InOut);
    } else if (a.enter == "fade") fade(n, a.at0, a.dur);
    std::vector<Node*> shapes{n}, groups{n};
    applyChanges(cx, L, shapes, groups, true);
    const json& lab = f(L, "labels");
    const bool markers = L.value("markers", false);
    int i = 0, cnt = (int)data.size();
    for (const auto& d : data) {
        if (!d.contains("y") || !d["y"].is_number()) { ++i; continue; }
        auto [x, y] = cx.pt(f(d, "x"), d["y"].get<double>());
        const float t = a.at0 + a.dur * (cnt > 1 ? (float)i / (cnt - 1) : 1);
        if (markers) {
            auto dot = std::make_unique<Node>();
            dot->kind = NodeKind::Circle;
            dot->x = x; dot->y = y; dot->radius = (float)num(L, "marker_size", 3.5); dot->fill = col;
            fade(lg->add(std::move(dot)), t, .2f);
        }
        if (labelWanted(lab, i, cnt)) {
            const json lo = lab.is_object() ? lab : json::object();
            const std::string pos = s(lo, "position", "top");
            const double v = d.contains("_v") ? d["_v"].get<double>() : d["y"].get<double>();
            if (pos == "right") valueLabel(cx, lg, lo, d, v, x + 10, y + 4, t + .2f, Align::Left);
            else valueLabel(cx, lg, lo, d, v, x, pos == "bottom" ? y + 16 : y - 9, t + .2f);
        }
        ++i;
    }
}

std::pair<float, float> rectSpan(Ctx& cx, const json& d, const char* k0, const char* k1, bool xAxis) {
    const Axis& A = xAxis ? cx.X : cx.Y;
    const bool screenX = xAxis != cx.horizontal;  // which screen axis this data axis maps to
    const float full0 = screenX ? cx.plot.x : cx.plot.y + cx.plot.h, full1 = screenX ? cx.plot.x + cx.plot.w : cx.plot.y;
    float a0 = full0, a1 = full1;
    if (d.contains(k0)) a0 = xAxis ? A.edge0(d[k0]) : A.lin(d[k0].get<double>());
    if (d.contains(k1)) a1 = xAxis ? A.edge1(d[k1]) : A.lin(d[k1].get<double>());
    return {std::min(a0, a1), std::max(a0, a1)};
}

void rectMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        auto [xa, xb] = rectSpan(cx, d, "x0", "x1", true);
        auto [ya, yb] = rectSpan(cx, d, "y0", "y1", false);
        float rx, ry, rw, rh;
        if (cx.horizontal) { rx = ya; rw = yb - ya; ry = xa; rh = xb - xa; }
        else { rx = xa; rw = xb - xa; ry = ya; rh = yb - ya; }
        Node* g = lg->add(group());
        const Color col = toneOf(cx, L, d, "accent");
        Node* r = g->add(rect(rx, ry, rw, rh, col.alpha((float)num(d, "opacity", num(L, "opacity", .12))), (float)num(L, "radius", 0)));
        if (L.contains("stroke")) { r->stroke = cx.kit.tone(L["stroke"].get<std::string>()); r->strokeW = (float)num(L, "width", 1.5); }
        const float t = datumTime(cx, d, a, i);
        if (a.enter == "grow") { r->oy = cx.horizontal ? 0 : rh; r->ox = cx.horizontal ? 0 : rw / 2;
            if (cx.horizontal) r->scaleX.fromTo(0, 1, t, a.dur, ease::p2Out); else r->scaleY.fromTo(0, 1, t, a.dur, ease::p2Out); }
        else if (a.enter != "none") fade(g, t, a.dur);
        shapes.push_back(r);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, false);
}

void ruleMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    const Color dflt = cx.kit.tone(s(L, "tone", "text2"));
    for (const auto& d : data) {
        const Color col = d.contains("tone") ? cx.kit.tone(d["tone"].get<std::string>()) : dflt;
        Node* g = lg->add(group());
        float x0, y0, x1, y1;
        const bool alongValue = d.contains("y");  // a value line spans the category axis
        if (alongValue) {
            auto [ca, cb] = rectSpan(cx, d, "x0", "x1", true);
            const float v = cx.Y.lin(d["y"].get<double>());
            if (cx.horizontal) { x0 = x1 = v; y0 = ca; y1 = cb; } else { x0 = ca; x1 = cb; y0 = y1 = v; }
        } else {
            const float c = cx.X.at(f(d, "x"));
            if (cx.horizontal) { x0 = cx.plot.x; x1 = cx.plot.x + cx.plot.w; y0 = y1 = c; } else { x0 = x1 = c; y0 = cx.plot.y; y1 = cx.plot.y + cx.plot.h; }
        }
        Node* ln = g->add(line(x0, y0, x1, y1, col, (float)num(L, "width", 1.5)));
        if (const json& dash = f(L, "dash"); dash.is_array()) for (const auto& v : dash) ln->dash.push_back(v.get<float>());
        if (d.contains("label")) {
            const std::string pos = s(d, "label_pos", s(L, "label_pos", "end"));
            const TextStyle st = style((float)num(L, "size", 11), 700, pos == "end" ? Align::Right : Align::Left);
            Node* lb = g->add(alongValue && !cx.horizontal ? text(pos == "end" ? x1 : x0 + 4, y0 - 6, d["label"].get<std::string>(), st, col)
                                                            : text(x0 + 4, std::min(y0, y1) + 12, d["label"].get<std::string>(), st, col));
            (void)lb;
        }
        const float t = datumTime(cx, d, a, i);
        if (a.enter == "rise" && alongValue) {
            const float from = cx.Y.lin(cx.base);
            if (cx.horizontal) g->tx.fromTo(from - x0, 0, t, a.dur, ease::p2Out); else g->ty.fromTo(from - y0, 0, t, a.dur, ease::p2Out);
            g->opacity.fromTo(0, 1, t, .2f, ease::p1Out);
        } else if (a.enter == "grow" || a.enter == "draw") {
            if (ln->dash.empty()) { ln->trim.fromTo(0, 1, t, a.dur, ease::p2InOut); for (auto& ch : g->children) if (ch.get() != ln) fade(ch.get(), t + a.dur * .8f, .3f); }
            else fade(g, t, .5f);
        } else if (a.enter != "none") fade(g, t, a.dur);
        shapes.push_back(ln);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, true);
}

void pointMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        auto [x, y] = cx.pt(f(d, "x"), num(d, "y", 0));
        x += (float)num(d, "dx", 0); y += (float)num(d, "dy", 0);
        const Color col = toneOf(cx, L, d, "accent");
        const float r = (float)num(d, "size", num(L, "size", 4));
        Node* g = lg->add(group(x, y));
        Node* m;
        if (s(d, "shape", s(L, "shape", "circle")) == "diamond")
            m = g->add(pathNode("M0 " + fs(-r * 1.6f) + " L" + fs(r * 1.6f) + " 0 L0 " + fs(r * 1.6f) + " L" + fs(-r * 1.6f) + " 0 Z", col));
        else {
            auto c = std::make_unique<Node>();
            c->kind = NodeKind::Circle; c->radius = r;
            if (s(d, "style", s(L, "style")) == "ring") { c->stroke = col; c->strokeW = 3; }
            else c->fill = col;
            m = g->add(std::move(c));
        }
        const float t = datumTime(cx, d, a, i);
        if (d.value("ping", L.value("ping", false))) {
            m->value.set(r, 0);
            for (int rep = 0; rep < 2; ++rep) {
                m->value.fromTo(r, r * 4, t + rep * 1.1f, 1.1f, ease::p2Out);
                m->opacity.fromTo(1, 0, t + rep * 1.1f, 1.1f, ease::p2Out);
            }
            m->opacity.set(0, 0);
            m->opacity.tweens.erase(m->opacity.tweens.begin());
            m->opacity.add({0, 0, 0, 0, {}});
        } else if (a.enter == "pop" || a.enter == "grow") { g->scale.fromTo(.3f, 1, t, .45f, ease::pop); g->opacity.fromTo(0, 1, t, .3f, ease::p1Out); }
        else if (a.enter != "none") fade(g, t, a.dur);
        shapes.push_back(m);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, false);
}

void textMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        float x, y;
        if (cx.polar && !d.contains("x")) {
            if (d.contains("theta")) {
                const float an = cx.ang(d["theta"].get<double>()) * (float)M_PI / 180, rr = cx.R * (float)num(d, "r", 1.14);
                x = cx.cx + rr * std::sin(an); y = cx.cy - rr * std::cos(an) + 4;
            } else { x = cx.cx; y = cx.cy; }
        } else if (d.contains("pos")) {
            const std::string p = d["pos"].get<std::string>();
            x = p.find("left") != std::string::npos ? cx.plot.x : p.find("right") != std::string::npos ? cx.plot.x + cx.plot.w : cx.plot.x + cx.plot.w / 2;
            y = p.find("top") != std::string::npos ? cx.plot.y + 12 : p.find("bottom") != std::string::npos ? cx.plot.y + cx.plot.h : cx.plot.y + cx.plot.h / 2;
        } else {
            std::tie(x, y) = cx.pt(f(d, "x"), num(d, "y", cx.base));
        }
        x += (float)num(d, "dx", 0);
        y += (float)num(d, "dy", 0);
        const std::string anchor = s(d, "anchor", s(L, "anchor", "middle"));
        const Align al = anchor == "start" ? Align::Left : anchor == "end" ? Align::Right : Align::Center;
        const float size = (float)num(d, "size", num(L, "size", 12));
        const TextStyle st = style(size, (int)num(d, "weight", num(L, "weight", 700)), al, (float)num(L, "letter_spacing", 0));
        const std::string box = s(d, "box", s(L, "box"));
        Color col = cx.kit.tone(s(d, "tone", s(L, "tone", box.empty() ? "text" : "")));
        if (!box.empty() && !d.contains("tone") && !L.contains("tone")) col = cx.kit.onTone(box);
        Node* g = lg->add(group());
        const float t = datumTime(cx, d, a, i);
        Node* tn;
        const bool counting = d.contains("value") && d["value"].is_number();
        if (counting) {
            const Fmt fm = Fmt::from(d.contains("format") ? d : L);
            const Locale loc = cx.kit.loc;
            tn = g->add(text(x, y, "", st, col));
            tn->format = [loc, fm](double v) { return fm(loc, v); };
            tn->value.fromTo((float)num(d, "from", 0), (float)d["value"].get<double>(), t, (float)num(d, "dur", num(L, "dur", 1.1)),
                             d.contains("from") ? ease::p3InOut : ease::p2Out);
        } else {
            const std::string str = s(d, "text");
            if (!box.empty()) {
                const float w = cx.kit.measure(str, st) + 2 * size * .9f, h = size * kNormalLine + size * .8f;
                const float bx = al == Align::Center ? x - w / 2 : al == Align::Right ? x - w + size * .9f : x - size * .9f;
                g->add(rect(bx, y - size * kAscent - size * .4f, w, h, cx.kit.tone(box), (float)num(L, "radius", 6)));
            }
            tn = g->add(text(x, y, str, st, col));
        }
        if (counting && a.enter != "none") { g->opacity.set(0, 0); enter(g, t, 0, 8, .35f); }
        else if (a.enter == "pop") { g->ox = x; g->oy = y; pop(g, t, 8, .9f); }
        else if (a.enter == "slide") enter(g, t, (float)num(L, "from_dx", 0), (float)num(L, "from_dy", 8), .4f);
        else if (a.enter != "none") fade(g, t, a.dur);
        shapes.push_back(tn);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, false);
}

void arcMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    const float inner = (float)num(L, "inner", .62), outer = (float)num(L, "outer", 1.0);
    const float rmid = cx.R * (inner + outer) / 2, width = cx.R * (outer - inner);
    if (L.contains("track")) lg->add(arc(cx.cx, cx.cy, rmid, (float)cx.pstart, (float)(cx.pend - cx.pstart), cx.kit.tone(L["track"].get<std::string>()), width));
    std::vector<Node*> shapes, groups;
    double run = cx.pmin;
    int i = 0;
    for (const auto& d : data) {
        double t0, t1;
        if (d.contains("theta0")) { t0 = d["theta0"].get<double>(); t1 = num(d, "theta1", t0); }
        else { t0 = run; t1 = run + num(d, "value", 0); run = t1; }
        const float a0 = cx.ang(t0), a1 = cx.ang(t1);
        const float gap = (float)num(L, "gap", 0);
        Node* g = lg->add(group());
        Node* n = g->add(arc(cx.cx, cx.cy, rmid, a0 + gap / 2, std::max(.1f, a1 - a0 - gap), toneOf(cx, L, d, "accent"), width));
        const float t = datumTime(cx, d, a, i);
        if (a.enter == "grow" || a.enter == "draw") { n->trim.set(0, 0); n->trim.fromTo(0, 1, t, a.dur, ease::p2InOut); }
        else if (a.enter != "none") fade(g, t, a.dur);
        if (d.contains("label")) {
            const float am = (a0 + a1) / 2 * (float)M_PI / 180;
            const bool inside = s(L, "label_pos", "outside") == "inside";
            const float rr = inside ? rmid : cx.R * outer + 14;
            const float lx = cx.cx + rr * std::sin(am), ly = cx.cy - rr * std::cos(am) + 4;
            const Align al = inside ? Align::Center : std::sin(am) > .2f ? Align::Left : std::sin(am) < -.2f ? Align::Right : Align::Center;
            fade(g->add(text(lx, ly, d["label"].get<std::string>(), style((float)num(L, "size", 11.5), 700, al),
                             inside ? cx.kit.onTone(s(d, "tone", s(L, "tone", "accent"))) : cx.kit.tone(s(L, "label_tone", "text2")))),
                 t + a.dur * .8f, .3f);
        }
        shapes.push_back(n);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, true);
}

void needleMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    int i = 0;
    for (const auto& d : data) {
        const float len = cx.R * (float)num(L, "length", .82);
        const Color col = toneOf(cx, L, d, "text");
        Node* g = lg->add(group(cx.cx, cx.cy));
        Node* ln = g->add(line(0, 0, 0, -len, col, (float)num(L, "width", 5)));
        ln->roundCap = true;
        auto hub = std::make_unique<Node>();
        hub->kind = NodeKind::Circle; hub->radius = (float)num(L, "width", 5) * 2; hub->fill = col;
        g->add(std::move(hub));
        const float t = datumTime(cx, d, a, i);
        const float to = cx.ang(num(d, "value", cx.pmin)), from = cx.ang(num(d, "from", cx.pmin));
        g->rotation.fromTo(from, to, t, a.dur, ease::p3InOut);
        ++i;
    }
}

void ribbonMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        const json &ya = f(d, "y0"), &yb = f(d, "y1");
        if (!ya.is_array() || !yb.is_array()) continue;
        auto [x0, a0] = cx.pt(f(d, "x0"), ya[0].get<double>());
        auto [x0b, b0] = cx.pt(f(d, "x0"), ya[1].get<double>());
        auto [x1, a1] = cx.pt(f(d, "x1"), yb[0].get<double>());
        auto [x1b, b1] = cx.pt(f(d, "x1"), yb[1].get<double>());
        (void)x0b; (void)x1b;
        const float xm = (x0 + x1) / 2;
        const std::string p = "M" + fs(x0) + " " + fs(a0) + " C" + fs(xm) + " " + fs(a0) + "," + fs(xm) + " " + fs(a1) + "," + fs(x1) + " " + fs(a1) +
                              " L" + fs(x1) + " " + fs(b1) + " C" + fs(xm) + " " + fs(b1) + "," + fs(xm) + " " + fs(b0) + "," + fs(x0) + " " + fs(b0) + " Z";
        const float t = datumTime(cx, d, a, i);
        Node* holder = (a.enter == "grow" || a.enter == "draw") ? revealGroup(cx, lg, t, a.dur) : lg->add(group());
        const Color col = toneOf(cx, L, d, "accent");
        Node* n = holder->add(pathNode(p, col.alpha((float)num(d, "opacity", num(L, "opacity", .35)))));
        if (a.enter == "fade") fade(holder, t, a.dur);
        shapes.push_back(n);
        groups.push_back(holder);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, false);
}

void pathMark(Ctx& cx, Node* lg, const json& L, const std::vector<json>& data, const Anim& a) {
    std::vector<Node*> shapes, groups;
    int i = 0;
    for (const auto& d : data) {
        std::string p;
        bool first = true;
        for (const auto& q : f(d, "points")) {
            auto [x, y] = cx.pt(q[0], q[1].get<double>());
            if (q.size() > 2) y += q[2].get<float>();
            if (q.size() > 3) x += q[3].get<float>();
            p += (first ? "M" : "L") + fs(x) + " " + fs(y) + " ";
            first = false;
        }
        if (first) continue;
        const bool closed = d.value("closed", false);
        if (closed) p += "Z";
        const Color col = toneOf(cx, L, d, "text2");
        Node* g = lg->add(group());
        Node* n = closed ? g->add(pathNode(p, col.alpha((float)num(L, "opacity", 1)))) : g->add(pathNode(p, Color{}, col, (float)num(L, "width", 2)));
        if (d.contains("label")) {
            const auto& pts = d["points"];
            auto [lx, ly] = cx.pt(pts[0][0], pts[0][1].get<double>());
            auto [rx, ry] = cx.pt(pts[pts.size() - 1][0], pts[pts.size() - 1][1].get<double>());
            float top = std::min(ly, ry);
            for (const auto& q : pts) { auto [qx, qy] = cx.pt(q[0], q[1].get<double>()); top = std::min(top, qy + (q.size() > 2 ? q[2].get<float>() : 0)); (void)qx; }
            g->add(text((lx + rx) / 2, top - 8, d["label"].get<std::string>(), style((float)num(L, "size", 12), 700, Align::Center), col));
        }
        const float t = datumTime(cx, d, a, i);
        if ((a.enter == "grow" || a.enter == "draw") && !closed) { n->trim.fromTo(0, 1, t, a.dur, ease::p2InOut); for (auto& ch : g->children) if (ch.get() != n) fade(ch.get(), t + a.dur * .6f, .3f); }
        else if (a.enter == "slide") enter(g, t, 0, 6, .45f);
        else if (a.enter != "none") fade(g, t, a.dur);
        shapes.push_back(n);
        groups.push_back(g);
        ++i;
    }
    applyChanges(cx, L, shapes, groups, true);
}

Anim animOf(Ctx& cx, const json& L, const std::string& mark) {
    const json& an = f(L, "animate");
    static const std::map<std::string, std::string> dflt = {{"bar", "grow"}, {"line", "draw"}, {"area", "grow"}, {"rect", "fade"}, {"rule", "draw"},
                                                            {"point", "pop"}, {"text", "fade"}, {"arc", "grow"}, {"needle", "grow"},
                                                            {"ribbon", "grow"}, {"path", "draw"}};
    Anim a;
    a.enter = an.is_object() ? s(an, "enter", dflt.count(mark) ? dflt.at(mark) : "fade") : (dflt.count(mark) ? dflt.at(mark) : "fade");
    a.at0 = cx.kit.at(an.is_object() && an.contains("at") ? an["at"] : f(cx.c, "at"), .3f);
    const size_t n = f(L, "data").size();
    const float st = mark == "bar" ? std::min(.08f, 1.2f / std::max<size_t>(1, n)) : mark == "text" || mark == "point" ? .06f : mark == "ribbon" ? .1f : 0;
    a.stagger = an.is_object() ? (float)num(an, "stagger", st) : st;
    const float dd = a.enter == "draw" || mark == "line" ? 1.2f : a.enter == "grow" ? (mark == "arc" || mark == "needle" ? 1.3f : .6f) : .4f;
    a.dur = an.is_object() ? (float)num(an, "dur", dd) : dd;
    return a;
}

void axisLabels(Ctx& cx, Node* g, const json& spec, const Axis& A, bool valueAxis, bool screenX) {
    const Kit& kit = cx.kit;
    if (spec.is_object() && spec.contains("labels") && spec["labels"].is_boolean() && !spec["labels"].get<bool>()) return;
    const Theme& th = kit.th;
    if (A.band) {
        std::vector<std::string> emph;
        if (spec.is_object()) for (const auto& e : f(spec, "emphasis")) emph.push_back(e.get<std::string>());
        const float size = spec.is_object() ? (float)num(spec, "size", A.cats.size() > 14 ? 9 : 10.5) : (A.cats.size() > 14 ? 9.f : 10.5f);
        for (size_t i = 0; i < A.cats.size(); ++i) {
            const bool e = std::find(emph.begin(), emph.end(), A.cats[i]) != emph.end();
            const float p = A.p0 + A.slot() * (i + .5f);
            if (screenX) g->add(text(p, cx.plot.y + cx.plot.h + 16, A.cats[i], style(size, e ? 700 : 500, Align::Center), e ? th.text : th.text3));
            else g->add(text(cx.plot.x - 10, p + 4, A.cats[i], style(size + 1, e ? 700 : 600, Align::Right), e ? th.text : th.text2));
        }
        return;
    }
    if (!valueAxis) return;
    const Fmt fm = Fmt::from(spec);
    const bool grid = !(spec.is_object() && f(spec, "grid").is_boolean() && !spec["grid"].get<bool>());
    for (double v = A.min; v <= A.max + 1e-9; v += A.step) {
        const float p = A.lin(v);
        Fmt f2 = fm;
        if (f2.decimals < 0 && f2.format.empty()) f2.decimals = axisDecimals(A.step / fm.divide);
        const std::string lab = f2(kit.loc, v);
        if (screenX) {
            if (grid) g->add(line(p, cx.plot.y, p, cx.plot.y + cx.plot.h, th.grid, 1));
            g->add(text(p, cx.plot.y + cx.plot.h + 14, lab, style(9.5f, 400, Align::Center), th.text3));
        } else {
            if (grid) g->add(line(cx.plot.x, p, cx.plot.x + cx.plot.w, p, th.grid, 1));
            g->add(text(cx.plot.x - 8, p + 3, lab, style(9.5f, 400, Align::Right), th.text3));
        }
    }
}

}  // namespace

void chart(Kit& kit, Node* parent, const json& c, Rect r) {
    const Theme& th = kit.th;
    Node* g = parent->add(group(r.x, r.y));
    Ctx cx{kit, c};
    cx.horizontal = s(c, "orient") == "horizontal";
    cx.polar = s(c, "coord") == "polar";
    const auto& layers = f(c, "layers");
    if (!layers.is_array()) throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": chart without layers");

    // Resolve data and transforms first; scales depend on them.
    std::vector<std::vector<json>> data;
    std::map<std::string, std::pair<double, double>> stacks;
    for (const auto& L : layers) {
        data.push_back(dataOf(L));
        transform(L, data.back(), stacks);
    }

    // Legend row.
    float top = 0;
    if (const json& lg = f(c, "legend"); lg.is_array() && !lg.empty()) {
        float x = 0;
        for (const auto& it : lg) {
            const Color col = kit.tone(s(it, "tone", "accent"));
            const std::string m = s(it, "mark", "bar");
            if (m == "line" || m == "dash") { Node* l = g->add(line(x, 6, x + 16, 6, col, 2)); if (m == "dash") l->dash = {4, 3}; x += 22; }
            else if (m == "point") { auto d = std::make_unique<Node>(); d->kind = NodeKind::Circle; d->x = x + 5; d->y = 6; d->radius = 4; d->fill = col; g->add(std::move(d)); x += 16; }
            else { Node* b = g->add(rect(x, 1, 10, 10, col, 2)); if (s(it, "pattern") == "hatch") { b->shader = hatchShader(col.alpha(.18f), col.alpha(.8f), 4, 1.5f); b->stroke = col; b->strokeW = 1; } x += 16; }
            const std::string lab = s(it, "label");
            g->add(text(x, 10, lab, style(11, 500), th.text2));
            x += kit.measure(lab, style(11, 500)) + 18;
        }
        top = 22;
    }

    if (cx.polar) {
        const json& p = f(c, "polar");
        cx.pstart = num(p, "start", 0);
        cx.pend = num(p, "end", 360);
        cx.pmin = num(p, "min", 0);
        double sum = 0;
        for (size_t i = 0; i < data.size(); ++i)
            if (s(layers[i], "mark") == "arc") { double t = 0; for (const auto& d : data[i]) t += num(d, "value", 0); sum = std::max(sum, t); }
        cx.pmax = num(p, "max", sum > 0 ? cx.pmin + sum : cx.pmin + 1);
        // Angle categories (radar spokes) and the radial value scale of the other marks.
        if (p.contains("domain")) for (const auto& v : p["domain"]) cx.pcats.push_back(v.is_string() ? v.get<std::string>() : v.dump());
        double rhi = 0;
        for (size_t i = 0; i < data.size(); ++i) {
            if (s(layers[i], "mark") == "arc") continue;
            for (const auto& d : data[i]) {
                if (!p.contains("domain") && d.contains("x") && d["x"].is_string() &&
                    std::find(cx.pcats.begin(), cx.pcats.end(), d["x"].get<std::string>()) == cx.pcats.end())
                    cx.pcats.push_back(d["x"].get<std::string>());
                if (d.contains("y") && d["y"].is_number()) rhi = std::max(rhi, d["y"].get<double>());
            }
        }
        const json& rs = f(p, "r");
        const double rstep = num(rs, "step", niceStep(rhi > 0 ? rhi : 1));
        cx.rmin = num(rs, "min", 0);
        cx.rmax = num(rs, "max", rhi > 0 ? std::ceil(rhi / rstep) * rstep : 1);
        if (cx.rmax <= cx.rmin) cx.rmax = cx.rmin + 1;
        const bool spokes = !cx.pcats.empty();
        const float h = r.h - top;
        const bool half = std::fabs(cx.pend - cx.pstart) <= 180.5;
        cx.cx = r.w / 2 + (float)num(p, "dx", 0);
        cx.R = (float)num(p, "radius", half ? std::min(r.w / 2 - 40, h * .62f) : std::min(r.w, h) / 2 - (spokes ? 34 : 22));
        cx.cy = top + (float)num(p, "cy", half ? (h * .66f) : h / 2);
        cx.plot = {0, top, r.w, h};
        if (spokes || f(p, "grid").is_boolean() && p["grid"].get<bool>()) {
            // Rings at each radial step (polygons on a radar), spokes and labels per category.
            const size_t n = cx.pcats.size();
            for (double v = cx.rmin + rstep; v <= cx.rmax + 1e-9; v += rstep) {
                const float rr = cx.R * (float)((v - cx.rmin) / (cx.rmax - cx.rmin));
                if (n >= 3) {
                    std::string ring;
                    for (size_t i = 0; i < n; ++i) {
                        const float an = cx.catAngle(i) * (float)M_PI / 180;
                        ring += (i ? "L" : "M") + fs(cx.cx + rr * std::sin(an)) + " " + fs(cx.cy - rr * std::cos(an)) + " ";
                    }
                    g->add(pathNode(ring + "Z", Color{}, th.grid, 1));
                } else {
                    auto c = std::make_unique<Node>();
                    c->kind = NodeKind::Circle; c->x = cx.cx; c->y = cx.cy; c->radius = rr; c->stroke = th.grid; c->strokeW = 1;
                    g->add(std::move(c));
                }
            }
            for (size_t i = 0; i < n; ++i) {
                const float an = cx.catAngle(i) * (float)M_PI / 180, sn = std::sin(an), cs = std::cos(an);
                g->add(line(cx.cx, cx.cy, cx.cx + cx.R * sn, cx.cy - cx.R * cs, th.grid, 1));
                const Align al = sn > .2f ? Align::Left : sn < -.2f ? Align::Right : Align::Center;
                g->add(text(cx.cx + (cx.R + 12) * sn, cx.cy - (cx.R + 12) * cs + 4 + (cs < -.5f ? 8 : 0), cx.pcats[i], style(11.5f, 600, al), th.text2));
            }
        }
        for (const auto& tk : f(p, "ticks")) {
            const float an = cx.ang(tk.get<double>()) * (float)M_PI / 180, rr = cx.R + 26;
            g->add(text(cx.cx + rr * std::sin(an), cx.cy - rr * std::cos(an) + 4, Fmt::from(p)(kit.loc, tk.get<double>()), style(11, 600, Align::Center), th.text3));
        }
    } else {
        // Category / x axis.
        const json& xs = f(c, "x");
        cx.X.band = s(xs, "type", "band") == "band";
        if (cx.X.band) {
            if (xs.is_object() && xs.contains("domain")) for (const auto& v : xs["domain"]) cx.X.cats.push_back(v.is_string() ? v.get<std::string>() : v.dump());
            else
                for (const auto& D : data)
                    for (const auto& d : D)
                        if (d.contains("x") && d["x"].is_string() && std::find(cx.X.cats.begin(), cx.X.cats.end(), d["x"].get<std::string>()) == cx.X.cats.end())
                            cx.X.cats.push_back(d["x"].get<std::string>());
            if (cx.X.cats.empty()) cx.X.cats.push_back("");
        } else {
            cx.X.min = num(xs, "min", 0);
            cx.X.max = num(xs, "max", 1);
            cx.X.step = num(xs, "step", (cx.X.max - cx.X.min) / 4);
        }
        // Value axis: nice auto range over everything drawn on it, unless given.
        // Layers with axis: "y2" get their own scale, labelled on the right (two units on one chart).
        std::vector<double> vals, vals2;
        for (size_t i = 0; i < data.size(); ++i) {
            const std::string m = s(layers[i], "mark");
            if (m == "text" || m == "path") continue;
            auto& out = s(layers[i], "axis") == "y2" ? vals2 : vals;
            for (const auto& d : data[i]) {
                // After a waterfall or stack, y is a delta: only y0/y1 are positions.
                const bool spans = d.contains("y0") && d.contains("y1") && d["y1"].is_number();
                for (const char* k : {"y", "y0", "y1"})
                    if (d.contains(k) && d[k].is_number() && !(spans && k[1] == 0)) out.push_back(d[k].get<double>());
                for (const char* k : {"y0", "y1"}) if (d.contains(k) && d[k].is_array()) for (const auto& v : d[k]) out.push_back(v.get<double>());
            }
        }
        auto fit = [&](const std::vector<double>& vs, const json& ys, Axis& A, double& base, const char* name) {
            double mx = 0, mn = 0;
            const bool zero = !(f(ys, "zero").is_boolean() && !ys["zero"].get<bool>());
            if (!vs.empty()) { mx = *std::max_element(vs.begin(), vs.end()); mn = *std::min_element(vs.begin(), vs.end()); }
            if (zero) { mx = std::max(mx, 0.0); mn = std::min(mn, 0.0); }
            const double span = std::max({std::fabs(mx), std::fabs(mn), (mx - mn) * .6, 1e-9});
            double step = niceStep(zero ? span : (mx - mn) * 1.0);
            double hi = std::ceil(mx * (mx > 0 ? 1.08 : .92) / step) * step, lo = mn < 0 || !zero ? std::floor(mn * (mn < 0 ? 1.08 : .92) / step) * step : 0;
            if (hi <= lo) hi = lo + step;
            A.min = num(ys, "min", lo);
            A.max = num(ys, "max", hi);
            A.step = num(ys, "step", step);
            if (ys.is_object() && (ys.contains("min") || ys.contains("max")))
                for (double v : vs)
                    if (v > A.max + 1e-9 || v < A.min - 1e-9) {
                        kit.warnings.push_back("scene " + std::to_string(kit.k + 1) + " \xc2\xb7 AXIS \xc2\xb7 chart value " + kit.loc.nf(v, 0) + " is outside " + name + " " +
                                               kit.loc.nf(A.min, 0) + ".." + kit.loc.nf(A.max, 0));
                        break;
                    }
            base = std::clamp(0.0, A.min, A.max);
        };
        const json& ys = f(c, "y");
        const json& ys2 = f(c, "y2");
        const bool hasY2 = !cx.horizontal && std::any_of(layers.begin(), layers.end(), [](const json& L) { return s(L, "axis") == "y2"; });
        fit(vals, ys, cx.Y, cx.base, "y");
        if (hasY2) fit(vals2, ys2, cx.Y2, cx.base2, "y2");

        // Plot area: room for labels on the left/bottom, margins on request.
        const json& m = f(c, "margin");
        const bool yLabels = !(f(ys, "labels").is_boolean() && !ys["labels"].get<bool>());
        const bool xLabels = !(f(xs, "labels").is_boolean() && !xs["labels"].get<bool>());
        float left = 4, bottom = 6;
        if (cx.horizontal) {
            if (xLabels && cx.X.band) for (const auto& cat : cx.X.cats) left = std::max(left, kit.measure(cat, style(11.5f, 600)) + 16);
            if (yLabels) bottom = 20;
        } else {
            if (yLabels) {
                const Fmt fm = Fmt::from(ys);
                Fmt f2 = fm;
                if (f2.decimals < 0 && f2.format.empty()) f2.decimals = axisDecimals(cx.Y.step / fm.divide);
                for (double v = cx.Y.min; v <= cx.Y.max + 1e-9; v += cx.Y.step) left = std::max(left, kit.measure(f2(kit.loc, v), style(9.5f, 400)) + 12);
            }
            if (xLabels) bottom = 24;
        }
        left = (float)num(m, "left", left);
        bottom = (float)num(m, "bottom", bottom);
        // y2 labels, formatted like the left ones.
        auto y2Label = [&](double v) {
            Fmt f2 = Fmt::from(ys2);
            if (f2.decimals < 0 && f2.format.empty()) f2.decimals = axisDecimals(cx.Y2.step / f2.divide);
            return f2(kit.loc, v);
        };
        const bool y2Labels = hasY2 && !(f(ys2, "labels").is_boolean() && !ys2["labels"].get<bool>());
        float right0 = cx.horizontal ? 48.f : 8.f;
        if (y2Labels) for (double v = cx.Y2.min; v <= cx.Y2.max + 1e-9; v += cx.Y2.step) right0 = std::max(right0, kit.measure(y2Label(v), style(9.5f, 400)) + 12);
        const float right = (float)num(m, "right", right0), mtop = (float)num(m, "top", 18);
        cx.plot = {left, top + mtop, r.w - left - right, r.h - top - mtop - bottom};
        if (cx.horizontal) {
            cx.X.p0 = cx.plot.y; cx.X.p1 = cx.plot.y + cx.plot.h;
            cx.Y.p0 = cx.plot.x; cx.Y.p1 = cx.plot.x + cx.plot.w;
        } else {
            cx.X.p0 = cx.plot.x; cx.X.p1 = cx.plot.x + cx.plot.w;
            cx.Y.p0 = cx.plot.y + cx.plot.h; cx.Y.p1 = cx.plot.y;
        }
        cx.Y2.p0 = cx.Y.p0; cx.Y2.p1 = cx.Y.p1;
        if (y2Labels) {
            const Color col = kit.tone(s(ys2, "tone", "text3"));
            for (double v = cx.Y2.min; v <= cx.Y2.max + 1e-9; v += cx.Y2.step)
                g->add(text(cx.plot.x + cx.plot.w + 8, cx.Y2.lin(v) + 3, y2Label(v), style(9.5f, 400, Align::Left), col));
        }
        axisLabels(cx, g, ys, cx.Y, true, cx.horizontal);
        axisLabels(cx, g, xs, cx.X, !cx.X.band, !cx.horizontal);
        if (cx.Y.min < 0 && cx.Y.max > 0) {
            const float z = cx.Y.lin(0);
            if (cx.horizontal) g->add(line(z, cx.plot.y, z, cx.plot.y + cx.plot.h, th.text3, 1));
            else g->add(line(cx.plot.x, z, cx.plot.x + cx.plot.w, z, th.text3, 1));
        } else if (!yLabels) {
            const float z = cx.Y.lin(cx.base);
            if (cx.horizontal) g->add(line(z, cx.plot.y, z, cx.plot.y + cx.plot.h, th.line, 1));
            else g->add(line(cx.plot.x, z, cx.plot.x + cx.plot.w, z, th.line, 1));
        }
    }

    for (size_t i = 0; i < layers.size(); ++i) {
        const json& L = layers[i];
        const std::string mark = s(L, "mark");
        Node* lg = g->add(group());
        const Anim a = animOf(cx, L, mark);
        const bool onY2 = !cx.polar && !cx.horizontal && s(L, "axis") == "y2";
        if (onY2) { std::swap(cx.Y, cx.Y2); std::swap(cx.base, cx.base2); }
        if (mark == "bar") bars(cx, lg, L, data[i], a);
        else if (mark == "line") lineMark(cx, lg, L, data[i], a, false);
        else if (mark == "area") lineMark(cx, lg, L, data[i], a, true);
        else if (mark == "rect") rectMark(cx, lg, L, data[i], a);
        else if (mark == "rule" && cx.polar) needleMark(cx, lg, L, data[i], a);
        else if (mark == "rule") ruleMark(cx, lg, L, data[i], a);
        else if (mark == "point") pointMark(cx, lg, L, data[i], a);
        else if (mark == "text") textMark(cx, lg, L, data[i], a);
        else if (mark == "arc") arcMark(cx, lg, L, data[i], a);
        else if (mark == "ribbon") ribbonMark(cx, lg, L, data[i], a);
        else if (mark == "path") pathMark(cx, lg, L, data[i], a);
        else throw std::runtime_error("scene " + std::to_string(kit.k + 1) + ": unknown mark '" + mark + "'");
        if (onY2) { std::swap(cx.Y, cx.Y2); std::swap(cx.base, cx.base2); }
    }
}
