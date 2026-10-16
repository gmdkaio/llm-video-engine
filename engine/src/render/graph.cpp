#include "render/graph.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkM44.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTextBlob.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkTrimPathEffect.h"
#include "include/utils/SkParsePath.h"
#include "render/text.h"

std::unique_ptr<Node> group(float x, float y) {
    auto n = std::make_unique<Node>();
    n->x = x; n->y = y;
    return n;
}

std::unique_ptr<Node> rect(float x, float y, float w, float h, Color fill, float radius) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Rect;
    n->x = x; n->y = y; n->w = w; n->h = h; n->fill = fill; n->radius = radius;
    return n;
}

std::unique_ptr<Node> line(float x1, float y1, float x2, float y2, Color stroke, float width) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Line;
    n->pts = {x1, y1, x2, y2};
    n->stroke = stroke; n->strokeW = width;
    return n;
}

std::unique_ptr<Node> text(float x, float y, std::string s, TextStyle st, Color fill) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Text;
    n->x = x; n->y = y; n->text = std::move(s); n->style = st; n->fill = fill;
    return n;
}

std::unique_ptr<Node> pathNode(const std::string& svgPath, Color fill, Color stroke, float width) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Path;
    auto p = SkParsePath::FromSVGString(svgPath.c_str());
    if (!p) throw std::runtime_error("bad path data: " + svgPath.substr(0, 60));
    n->path = std::make_shared<SkPath>(*p);
    n->fill = fill; n->stroke = stroke; n->strokeW = width;
    return n;
}

std::unique_ptr<Node> arc(float cx, float cy, float r, float startDeg, float sweepDeg, Color stroke, float width) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Path;
    SkPathBuilder b;
    // Skia measures angles from 3 o'clock; ours from 12 o'clock.
    b.addArc(SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), startDeg - 90, sweepDeg);
    n->path = std::make_shared<SkPath>(b.detach());
    n->stroke = stroke; n->strokeW = width;
    return n;
}

std::unique_ptr<Node> imageNode(float x, float y, float w, float h, sk_sp<SkImage> img) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Image;
    n->x = x; n->y = y; n->w = w; n->h = h; n->image = std::move(img);
    return n;
}

sk_sp<SkShader> hatchShader(Color background, Color stripe, float period, float width) {
    // Tile at 4x for smooth stripes; the shader matrix brings it back to logical size.
    const int k = 4, s = (int)std::lround(period * k);
    SkBitmap bm;
    bm.allocN32Pixels(s, s);
    SkCanvas c(bm);
    c.clear(background.argb());
    SkPaint p;
    p.setColor(stripe.argb());
    c.drawRect(SkRect::MakeXYWH(-width * k / 2, 0, width * k, (float)s), p);
    c.drawRect(SkRect::MakeXYWH(s - width * k / 2, 0, width * k, (float)s), p);
    SkMatrix m = SkMatrix::Scale(1.f / k, 1.f / k);
    m.postRotate(45);
    return bm.asImage()->makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, SkSamplingOptions(SkFilterMode::kLinear), &m);
}

static float at(const Track<float>& tr, float t, float dflt) { return tr.empty() ? dflt : tr.eval(t); }

static void strokePaint(SkPaint& p, const Node& n, float alpha, float t) {
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(n.strokeW);
    p.setColor((n.strokeColor.empty() ? n.stroke : n.strokeColor.eval(t)).argb(alpha));
    p.setStrokeJoin(SkPaint::kRound_Join);
    if (n.roundCap) p.setStrokeCap(SkPaint::kRound_Cap);
    if (!n.trim.empty()) p.setPathEffect(SkTrimPathEffect::Make(0, std::clamp(n.trim.eval(t), 0.f, 1.f)));
    else if (!n.dash.empty()) p.setPathEffect(SkDashPathEffect::Make({n.dash.data(), n.dash.size()}, 0));
}

struct Baked {
    std::once_flag once;
    sk_sp<SkImage> image;
};

Node* cache(Node* n) {
    n->baked = std::make_shared<Baked>();
    return n;
}

void prepare(Node& n, const Fonts& fonts) {
    if (n.kind == NodeKind::Text && !n.format && !n.text.empty()) {
        Shaped sh = shape(fonts, n.text, n.style);
        n.blob = sh.blob;
        n.blobWidth = sh.width;
    }
    for (auto& ch : n.children) prepare(*ch, fonts);
}

static void drawNode(SkCanvas* c, const Node& n, float t, const Fonts& fonts);

sk_sp<SkImage> rasterize(const Node& n, float ox, float oy, float w, float h, float pixelScale, const Fonts& fonts) {
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul((int)std::ceil(w * pixelScale), (int)std::ceil(h * pixelScale)));
    SkCanvas* c = surface->getCanvas();
    c->clear(SK_ColorTRANSPARENT);
    c->scale(pixelScale, pixelScale);
    c->translate(-ox, -oy);
    drawNode(c, n, 0, fonts);
    return surface->makeImageSnapshot();
}

namespace {
struct Strip { int w = 0, h = 0, y0 = 0; };
thread_local Strip gStrip;
}  // namespace

void setDrawStrip(int frameW, int frameH, int y0) { gStrip = {frameW, frameH, y0}; }

void draw(SkCanvas* c, const Node& n, float t, const Fonts& fonts) {
    if (!n.baked) return drawNode(c, n, t, fonts);
    // Gradients cost ~100 ns/pixel on the raster backend, so static layers are drawn once, at full
    // frame size even when this canvas is one strip of the frame.
    std::call_once(n.baked->once, [&] {
        const SkISize full = gStrip.w > 0 ? SkISize::Make(gStrip.w, gStrip.h) : c->imageInfo().dimensions();
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(full));
        surface->getCanvas()->setMatrix(SkM44::Translate(0, (float)gStrip.y0) * c->getLocalToDevice());
        drawNode(surface->getCanvas(), n, t, fonts);
        n.baked->image = surface->makeImageSnapshot();
    });
    c->save();
    c->resetMatrix();
    c->drawImage(n.baked->image, 0, (float)-gStrip.y0);
    c->restore();
}

static void drawNode(SkCanvas* c, const Node& n, float t, const Fonts& fonts) {
    float alpha = at(n.opacity, t, 1);
    if (alpha <= 0.001f) return;
    const float sx = at(n.scale, t, 1) * at(n.scaleX, t, 1);
    const float sy = at(n.scale, t, 1) * at(n.scaleY, t, 1);
    if (sx == 0 || sy == 0) return;
    const float rot = at(n.rotation, t, 0);

    c->save();
    c->translate(n.x + at(n.tx, t, 0), n.y + at(n.ty, t, 0));
    if (sx != 1 || sy != 1 || rot != 0) {
        c->translate(n.ox, n.oy);
        if (rot != 0) c->rotate(rot);
        c->scale(sx, sy);
        c->translate(-n.ox, -n.oy);
    }
    if (n.clip) {
        const float cw = at(n.clipWidth, t, n.clipW);
        if (cw <= 0) { c->restore(); return; }
        c->clipRect(SkRect::MakeXYWH(n.clipX, n.clipY, cw, n.clipH), true);
    }

    // Groups with several children fade as one layer, like CSS opacity. A blurred node is always a layer.
    const float bx = at(n.blurX, t, 0), by = at(n.blurY, t, 0);
    const bool blurred = bx > 0.01f || by > 0.01f;
    const bool layer = blurred || (n.kind == NodeKind::Group && alpha < 0.999f && !n.children.empty());
    if (blurred) {
        SkPaint lp;
        lp.setAlphaf(alpha);
        lp.setImageFilter(SkImageFilters::Blur(bx, by, nullptr));
        c->saveLayer(nullptr, &lp);
        alpha = 1;
    } else if (layer) {
        c->saveLayerAlphaf(nullptr, alpha);
        alpha = 1;
    }
    const Color fill = n.color.empty() ? n.fill : n.color.eval(t);
    auto fillPaint = [&](SkPaint& p) {
        p.setAntiAlias(true);
        if (n.shader) {
            p.setShader(n.shader);
            p.setAlphaf(alpha);
        } else {
            p.setColor(fill.argb(alpha));
        }
    };

    switch (n.kind) {
    case NodeKind::Group:
        break;
    case NodeKind::Rect: {
        const SkRect r = SkRect::MakeWH(n.w, n.h);
        if (n.shader || fill.a > 0) {
            SkPaint p;
            fillPaint(p);
            c->drawRoundRect(r, n.radius, n.radius, p);
        }
        if (n.strokeW > 0) {
            SkPaint p;
            strokePaint(p, n, alpha, t);
            c->drawRoundRect(r, n.radius, n.radius, p);
        }
        break;
    }
    case NodeKind::Line:
    case NodeKind::Polyline: {
        SkPathBuilder b;
        b.moveTo(n.pts[0], n.pts[1]);
        for (size_t i = 2; i + 1 < n.pts.size(); i += 2) b.lineTo(n.pts[i], n.pts[i + 1]);
        SkPaint p;
        strokePaint(p, n, alpha, t);
        c->drawPath(b.detach(), p);
        break;
    }
    case NodeKind::Path: {
        if (n.shader || fill.a > 0) {
            SkPaint p;
            fillPaint(p);
            c->drawPath(*n.path, p);
        }
        if (n.strokeW > 0) {
            SkPaint p;
            strokePaint(p, n, alpha, t);
            c->drawPath(*n.path, p);
        }
        break;
    }
    case NodeKind::Text: {
        sk_sp<SkTextBlob> blob = n.blob;
        float width = n.blobWidth;
        if (!blob) {
            Shaped sh = shape(fonts, n.format ? n.format(at(n.value, t, 0)) : n.text, n.style);
            blob = sh.blob;
            width = sh.width;
        }
        if (blob) {
            float dx = 0;
            if (n.style.align == Align::Center) dx = -width / 2;
            else if (n.style.align == Align::Right) dx = -width;
            SkPaint p;
            p.setAntiAlias(true);
            p.setColor(fill.argb(alpha));
            c->drawTextBlob(blob, dx, 0, p);
        }
        break;
    }
    case NodeKind::Circle: {
        const float r = n.value.empty() ? n.radius : n.value.eval(t);
        if (fill.a > 0) {
            SkPaint p;
            fillPaint(p);
            c->drawCircle(0, 0, r, p);
        }
        if (n.strokeW > 0) {
            SkPaint p;
            strokePaint(p, n, alpha, t);
            c->drawCircle(0, 0, r, p);
        }
        break;
    }
    case NodeKind::Image: {
        SkPaint p;
        p.setAlphaf(alpha);
        c->drawImageRect(n.image, SkRect::MakeWH(n.w, n.h), SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &p);
        break;
    }
    case NodeKind::Glow: {
        // CSS radial-gradient(<w>px <h>px at x y, colour, transparent 60%)
        const SkColor4f colors[2] = {SkColor4f::FromColor(fill.argb(alpha)), SkColor4f::FromColor(fill.alpha(0).argb())};
        const float stops[2] = {0, 0.6f};
        const SkGradient grad({colors, stops, SkTileMode::kClamp},
                              {SkGradient::Interpolation::InPremul::kYes});
        const SkMatrix m = SkMatrix::Scale(n.w, n.h);
        SkPaint p;
        p.setShader(SkShaders::RadialGradient({0, 0}, 1, grad, &m));
        c->drawPaint(p);
        break;
    }
    }

    for (const auto& ch : n.children) draw(c, *ch, t, fonts);
    if (layer) c->restore();
    c->restore();
}
