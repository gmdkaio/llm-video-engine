// Spike expanders: page frame (background, header, progress) and the `kpi`
// scene with a bars chart. Layout numbers mirror the reference template's CSS;
// line boxes use Montserrat's metrics (ascent .968, descent .251, normal line
// height 1.219 em).
#include <algorithm>
#include <cmath>
#include <string>

#include "core/timeline.h"
#include "render/text.h"
#include "render/theme.h"
#include "scenes/build.h"

using nlohmann::json;

namespace {

constexpr float kAscent = 0.968f, kNormalLine = 1.219f;
constexpr float kW = 1280, kH = 720;

const Ease kP1Out = Ease::parse("power1.out");
const Ease kP2Out = Ease::parse("power2.out");
const Ease kP2In = Ease::parse("power2.in");
const Ease kP2InOut = Ease::parse("power2.inOut");
const Ease kP3Out = Ease::parse("power3.out");
const Ease kP3InOut = Ease::parse("power3.inOut");
const Ease kPop = Ease::parse("back.out(1.6)");
const Ease kLinear = Ease::parse("none");

// Baseline of a single line whose box starts at `top`.
float baseline(float top, float size, float lineHeight) {
    const float content = size * kNormalLine;
    return top + (lineHeight - content) / 2 + size * kAscent;
}

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// pt-BR number formatting, as toLocaleString('pt-BR') does it.
std::string nf(double v, int decimals) {
    const double scale = std::pow(10.0, decimals);
    long long n = std::llround(std::fabs(v) * scale);
    std::string frac;
    for (int i = 0; i < decimals; ++i) { frac.insert(frac.begin(), char('0' + n % 10)); n /= 10; }
    std::string whole = std::to_string(n);
    for (int i = (int)whole.size() - 3; i > 0; i -= 3) whole.insert((size_t)i, ".");
    return (v < 0 && std::llround(std::fabs(v) * scale) != 0 ? "-" : "") + whole + (decimals ? "," + frac : "");
}

std::string fmtValue(double v, const std::string& f, int dec) {
    const std::string sign = v < 0 ? "−" : "";
    const double a = std::fabs(v);
    const int d0 = dec < 0 ? 0 : dec, d1 = dec < 0 ? 1 : dec;
    std::string s;
    if (f == "k") s = "R$ " + nf(a, d0) + "k";
    else if (f == "mil") s = "R$ " + nf(a, d0) + " mil";
    else if (f == "mi") s = "R$ " + nf(a, d1) + " mi";
    else if (f == "pct") s = nf(a, d1) + "%";
    else if (f == "x") s = nf(a, dec < 0 ? 2 : dec) + "×";
    else if (f == "dias") s = nf(a, d0) + " dias";
    else s = nf(a, d0);
    return sign + s;
}

struct Range { double max, min, step; };

double niceStep(double v) {
    const double m = v * 1.08;
    const double p = std::pow(10.0, std::floor(std::log10(m > 0 ? m : 1)));
    for (double x : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m / (x * p) <= 5) return x * p;
    return 10 * p;
}

Range range(const std::vector<double>& vals) {
    double mx = 0, mn = 0;
    for (double v : vals) { mx = std::max(mx, v); mn = std::min(mn, v); }
    const double span = std::max({mx, -mn, (mx - mn) * 0.6});
    const double step = niceStep(span);
    double top = std::ceil(mx * 1.08 / step) * step;
    if (top == 0) top = step;
    const double bottom = mn < 0 ? -std::ceil(-mn * 1.08 / step) * step : 0;
    return {top, bottom, step};
}

struct Ctx {
    const Fonts& fonts;
    const Theme& th;
    std::vector<float> B;
    int k = 0;
    float at(const json& spec, float dflt) const {
        const float rel = spec.is_number() ? spec.get<float>() : dflt;
        return B[k] + timeline::kLead + rel;
    }
};

const json& field(const json& o, const char* key) {
    static const json null;
    auto it = o.find(key);
    return it == o.end() ? null : *it;
}

void pop(Node* n, float t, float y = 14, float s = .96f) {
    n->opacity.fromTo(0, 1, t, .5f, kPop);
    n->ty.fromTo(y, 0, t, .5f, kPop);
    n->scale.fromTo(s, 1, t, .5f, kPop);
}

void fade(Node* n, float t, float d = .3f) { n->opacity.fromTo(0, 1, t, d, kP1Out); }

TextStyle style(float size, int weight, Align a = Align::Left, float ls = 0) {
    TextStyle st;
    st.size = size; st.weight = weight; st.align = a; st.letterSpacing = ls;
    return st;
}

// Bars chart drawn into an SVG-like box W x (H + 30), as drawChart() does.
void bars(Node* svg, const json& G, float W, float Hsvg, float t0, const Ctx& cx) {
    const Theme& th = cx.th;
    const float H = Hsvg - 30, L = 40;
    std::vector<double> V = G["values"].get<std::vector<double>>();
    std::vector<double> ref = G.contains("ref") ? G["ref"].get<std::vector<double>>() : std::vector<double>{};
    const auto cats = G["cats"].get<std::vector<std::string>>();
    const int n = (int)V.size();
    const float bw = (W - L) / n;

    std::vector<double> all = V;
    all.insert(all.end(), ref.begin(), ref.end());
    const Range r = range(all);
    auto y = [&](double v) { return (float)(H - (v - r.min) / (r.max - r.min) * (H - 30)); };

    for (double g = r.min; g <= r.max + 1e-9; g += r.step) {
        const float yy = y(g);
        if (std::fabs(g) < 1e-9 && r.min < 0) svg->add(line(L, yy, W, yy, th.text3, 1));
        svg->add(line(L, yy, W, yy, th.grid, 1));
        svg->add(text(L - 8, yy + 3, nf(g, r.step < 1 ? 1 : 0), style(9.5f, 400, Align::Right), th.text3));
    }

    const int hi = n - 1;
    for (int i = 0; i < n; ++i) {
        const double v = V[i];
        const float x = L + i * bw + bw * .2f, w = bw * .6f;
        const bool last = i == hi;
        svg->add(text(x + w / 2, H + 16, cats[i], style(n > 14 ? 8.5f : 10, last ? 700 : 500, Align::Center),
                      last ? th.text : th.text3));
        const float tt = t0 + i * std::min(.08f, 1.2f / n);
        const float top = std::min(y(v), y(0)), h = std::fabs(y(0) - y(v));
        Node* bar = svg->add(rect(x, top, w, h, last ? th.accent : th.accentDim, 3));
        bar->ox = w / 2;
        bar->oy = v < 0 ? 0 : h;
        bar->scaleY.fromTo(0, 1, tt, .6f, kP3Out);

        Node* lb = svg->add(text(x + w / 2, v < 0 ? y(v) + 14 : y(v) - 8, nf(v, std::fabs(v) < 10 ? 1 : 0),
                                 style(last ? 13 : (n > 14 ? 8.5f : 10), last ? 700 : 500, Align::Center),
                                 last ? th.text : th.text3));
        fade(lb, tt + .45f);
    }

    if (!ref.empty()) {
        Node* clip = svg->add(group());
        clip->clip = true;
        clip->clipW = 0; clip->clipH = H + 40;
        clip->clipWidth.to(W, cx.at(field(G, "at_ref"), t0 - cx.B[cx.k] - timeline::kLead), 1.2f, kP2InOut, 0);
        auto path = std::make_unique<Node>();
        path->kind = NodeKind::Polyline;
        for (int i = 0; i < (int)ref.size(); ++i) {
            path->pts.push_back(L + i * bw + bw * .5f);
            path->pts.push_back(y(ref[i]));
        }
        path->stroke = th.neutral; path->strokeW = 2; path->dash = {5, 5};
        clip->add(std::move(path));
    }
}

void kpi(Node* sc, const json& c, const Ctx& cx) {
    const Theme& th = cx.th;
    const Fonts& fonts = cx.fonts;
    Node* in = sc->add(group(28, 72));
    const bool hasChart = c.contains("chart");
    float top = 0;

    if (c.contains("kicker")) {
        const auto& kk = c["kicker"];
        const float base = baseline(0, 10.5f, 10.5f * kNormalLine);
        float x = 0;
        if (kk.contains("b")) {
            TextStyle st = style(10.5f, 700, Align::Left, 1.26f);
            const std::string s = upper(kk["b"].get<std::string>());
            in->add(text(0, base, s, st, th.accent));
            x = measure(fonts, s, st);
        }
        const std::string rest = (kk.contains("b") ? " · " : "") + upper(kk.value("t", ""));
        in->add(text(x, base, rest, style(10.5f, 600, Align::Left, 1.26f), th.text3));
        top = 10.5f * kNormalLine;
    }

    // Big counter: invisible until it starts counting.
    top += 14;
    if (c.contains("value")) {
        const auto& v = c["value"];
        TextStyle st = style(84, 700, Align::Left, -2.52f);
        st.tnum = true;
        Node* num = in->add(text(0, baseline(top, 84, 84), "", st, th.text));
        const std::string fmt = v.value("format", "k");
        const int dec = v.contains("decimals") ? v["decimals"].get<int>() : -1;
        num->format = [fmt, dec](double x) { return fmtValue(x, fmt, dec); };
        const float t = cx.at(field(v, "at"), 1);
        num->opacity.set(0, 0);
        num->opacity.fromTo(0, 1, t, .35f, kP1Out);
        num->ty.fromTo(10, 0, t, .35f, kP1Out);
        num->value.fromTo(0, v["num"].get<float>(), t, 1.1f, kP2Out);
    }
    top += 84 + 16;

    // Chips: inline-blocks, one per line next to a chart.
    const float chipH = 12.5f * kNormalLine + 12, chipLine = chipH + 8;
    float chipX = 0;
    int i = 0;
    for (const auto& ch : c.value("chips", json::array())) {
        const std::string tone = ch.value("tone", "good");
        Color bg = th.good.alpha(.15f), fg = th.goodText;
        if (tone == "neutral") { bg = th.text.alpha(.07f); fg = th.text2; }
        else if (tone == "bad") { bg = th.bad.alpha(.14f); fg = th.bad; }
        else if (tone == "warn") { bg = th.warn.alpha(.15f); fg = th.warn; }
        const std::string s = ch["text"].get<std::string>();
        const TextStyle st = style(12.5f, 600);
        const float w = measure(fonts, s, st) + 22;
        Node* g = in->add(group(hasChart ? 0 : chipX, hasChart ? top + i * chipLine : top));
        g->add(rect(0, 0, w, chipH, bg, 6));
        g->add(text(11, 6 + 12.5f * kAscent, s, st, fg));
        g->ox = w / 2; g->oy = chipH / 2;
        pop(g, cx.at(field(ch, "at"), 2 + i * 1.5f));
        chipX += w + 8;
        ++i;
    }
    top += hasChart ? i * chipLine : (i ? chipLine : 0);

    if (c.contains("lead")) {
        const auto& l = c["lead"];
        top += 14;
        const TextStyle st = style(14, 400);
        const float lh = 14 * 1.55f;
        Node* g = in->add(group(0, top));
        int li = 0;
        for (const auto& s : wrap(fonts, l["text"].get<std::string>(), st, hasChart ? 430 : 1100))
            g->add(text(0, baseline(li++ * lh, 14, lh), s, st, th.text2));
        g->ox = 215; g->oy = li * lh / 2;
        pop(g, cx.at(field(l, "at"), 5), 10, 1);
    }

    if (hasChart) {
        const auto& G = c["chart"];
        Node* panel = in->add(group(470, 0));
        panel->add(rect(0, 0, 754, 592, th.panel, 12));
        const float tb = 16 + 12 * kAscent;
        const std::string title = G.value("title", "");
        panel->add(text(18, tb, title, style(12, 600), th.text));
        if (G.contains("sub")) {
            const float x = 18 + measure(fonts, title + " ", style(12, 600)) + 8;
            panel->add(text(x, tb, G["sub"].get<std::string>(), style(10.5f, 500), th.text3));
        }
        Node* svg = panel->add(group(18, 16 + 12 * kNormalLine + 10));
        bars(svg, G, 718, 530, cx.at(field(G, "at"), -.1f), cx);
    }
}

}  // namespace

Video buildVideo(const json& plan, const Fonts& fonts) {
    const auto& tk = plan["theme"];
    const Theme th = Theme::make(Color::hex(tk.value("bg", "#0e0f10")), Color::hex(tk.value("text", "#f1f1ef")),
                                 Color::hex(tk.value("accent", "#3987e5")), Color::hex(tk.value("good", "#2fb344")),
                                 Color::hex(tk.value("bad", "#e66767")), Color::hex(tk.value("warn", "#fab219")));
    const auto& scenes = plan["scenes"];
    std::vector<std::string> speeches;
    for (const auto& s : scenes) speeches.push_back(s.value("speech", ""));
    Ctx cx{fonts, th, timeline::boundaries(speeches)};
    const float total = cx.B.back();

    Video v;
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

    // Header: tabs follow the scenes; it fades in with the first scene that shows it.
    Node* hdr = root->add(group());
    hdr->opacity.set(0, 0);
    hdr->add(rect(0, 45, kW, 1, th.line));
    std::vector<std::string> tabs;
    for (const auto& s : scenes) {
        const std::string tab = s.value("tab", "");
        if (!tab.empty() && std::find(tabs.begin(), tabs.end(), tab) == tabs.end()) tabs.push_back(tab);
    }
    const TextStyle tabSt = style(11, 500);
    const float tabBase = -0.5f + baseline((46 - 11 * kNormalLine) / 2, 11, 11 * kNormalLine);
    std::vector<Node*> tabNodes;
    std::vector<float> tabLeft, tabWidth;
    float tx = 0;
    for (const auto& t : tabs) {
        const float w = measure(fonts, t, tabSt) + 22;
        tabNodes.push_back(hdr->add(text(62 + tx + 11, tabBase, t, tabSt, th.text3)));
        tabLeft.push_back(tx + 8);
        tabWidth.push_back(w - 16);
        tx += w;
    }
    Node* tabbar = hdr->add(rect(62, -0.5f + 46 + 1 - 2, 1, 2, th.accent, 1));
    if (const auto period = plan.value("header", json::object()).value("period", ""); !period.empty()) {
        const TextStyle st = style(10.5f, 600, Align::Right, 1.26f);
        hdr->add(text(kW - 28, baseline((45 - 10.5f * kNormalLine) / 2, 10.5f, 10.5f * kNormalLine), upper(period),
                      st, th.text3));
    }

    for (int k = 0; k < (int)scenes.size(); ++k) {
        const auto& c = scenes[k];
        cx.k = k;
        const float S0 = cx.B[k], S1 = cx.B[k + 1];
        Node* sc = scenesNode->add(group());
        sc->opacity.set(0, 0);
        sc->opacity.fromTo(0, 1, S0, .6f, kP3Out);
        sc->tx.fromTo(30, 0, S0, .6f, kP3Out);
        if (k + 1 < (int)scenes.size()) {
            sc->opacity.to(0, S1 - .45f, .45f, kP2In, 1);
            sc->tx.to(-30, S1 - .45f, .45f, kP2In, 0);
        }
        hdr->opacity.to(1, S0, .4f, kP1Out, 0);
        if (const std::string tab = c.value("tab", ""); !tab.empty()) {
            const size_t ti = std::find(tabs.begin(), tabs.end(), tab) - tabs.begin();
            tabbar->tx.to(tabLeft[ti], S0 + .1f, .5f, kP3InOut, 0);
            tabbar->scaleX.to(tabWidth[ti], S0 + .1f, .5f, kP3InOut, 0);
            for (size_t j = 0; j < tabNodes.size(); ++j)
                tabNodes[j]->color.to(j == ti ? th.text : th.text3, S0 + .1f, .3f, kP1Out, th.text3);
        }
        const std::string type = c.value("type", "");
        if (type == "kpi") kpi(sc, c, cx);
        else throw std::runtime_error("spike only supports kpi scenes, got: " + type);
    }

    Node* prog = root->add(rect(0, kH - 3, kW, 3, th.accent));
    prog->scaleX.fromTo(0, 1, 0, total, kLinear);
    return v;
}
