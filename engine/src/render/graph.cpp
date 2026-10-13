#include "render/graph.h"

#include <mutex>

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkSurface.h"
#include "include/core/SkM44.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
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

static float at(const Track<float>& tr, float t, float dflt) { return tr.empty() ? dflt : tr.eval(t); }

static void strokePaint(SkPaint& p, const Node& n, float alpha) {
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(n.strokeW);
    p.setColor(n.stroke.argb(alpha));
    if (!n.dash.empty()) p.setPathEffect(SkDashPathEffect::Make({n.dash.data(), n.dash.size()}, 0));
}

struct Baked {
    std::once_flag once;
    sk_sp<SkImage> image;
};

Node* cache(Node* n) {
    n->baked = std::make_shared<Baked>();
    return n;
}

static void drawNode(SkCanvas* c, const Node& n, float t, const Fonts& fonts);

void draw(SkCanvas* c, const Node& n, float t, const Fonts& fonts) {
    if (!n.baked) return drawNode(c, n, t, fonts);
    // Gradients cost ~100 ns/pixel on the raster backend, so static layers are drawn once.
    std::call_once(n.baked->once, [&] {
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(c->imageInfo().dimensions()));
        surface->getCanvas()->setMatrix(c->getLocalToDevice());
        drawNode(surface->getCanvas(), n, t, fonts);
        n.baked->image = surface->makeImageSnapshot();
    });
    c->save();
    c->resetMatrix();
    c->drawImage(n.baked->image, 0, 0);
    c->restore();
}

static void drawNode(SkCanvas* c, const Node& n, float t, const Fonts& fonts) {
    float alpha = at(n.opacity, t, 1);
    if (alpha <= 0) return;
    const float sx = at(n.scale, t, 1) * at(n.scaleX, t, 1);
    const float sy = at(n.scale, t, 1) * at(n.scaleY, t, 1);
    if (sx == 0 || sy == 0) return;

    c->save();
    c->translate(n.x + at(n.tx, t, 0), n.y + at(n.ty, t, 0));
    if (sx != 1 || sy != 1) {
        c->translate(n.ox, n.oy);
        c->scale(sx, sy);
        c->translate(-n.ox, -n.oy);
    }
    if (n.clip) c->clipRect(SkRect::MakeXYWH(n.clipX, n.clipY, at(n.clipWidth, t, n.clipW), n.clipH), true);

    // Groups with several children fade as one layer, like CSS opacity.
    const bool layer = n.kind == NodeKind::Group && alpha < 1 && !n.children.empty();
    if (layer) {
        c->saveLayerAlphaf(nullptr, alpha);
        alpha = 1;
    }
    const Color fill = n.color.empty() ? n.fill : n.color.eval(t);

    switch (n.kind) {
    case NodeKind::Group:
        break;
    case NodeKind::Rect: {
        const SkRect r = SkRect::MakeWH(n.w, n.h);
        if (n.fill.a > 0 || !n.color.empty()) {
            SkPaint p;
            p.setAntiAlias(true);
            p.setColor(fill.argb(alpha));
            c->drawRoundRect(r, n.radius, n.radius, p);
        }
        if (n.strokeW > 0) {
            SkPaint p;
            strokePaint(p, n, alpha);
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
        strokePaint(p, n, alpha);
        p.setStrokeJoin(SkPaint::kRound_Join);
        c->drawPath(b.detach(), p);
        break;
    }
    case NodeKind::Text: {
        const std::string s = n.format ? n.format(at(n.value, t, 0)) : n.text;
        Shaped sh = shape(fonts, s, n.style);
        if (sh.blob) {
            float dx = 0;
            if (n.style.align == Align::Center) dx = -sh.width / 2;
            else if (n.style.align == Align::Right) dx = -sh.width;
            SkPaint p;
            p.setAntiAlias(true);
            p.setColor(fill.argb(alpha));
            c->drawTextBlob(sh.blob, dx, 0, p);
        }
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
