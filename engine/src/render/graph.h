#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/core/SkTextBlob.h"
#include "core/tracks.h"
#include "render/color.h"

class SkCanvas;
class SkPath;
struct Fonts;
struct Baked;

enum class NodeKind { Group, Rect, Line, Polyline, Text, Glow, Path, Image, Circle };
enum class Align { Left, Center, Right };

struct TextStyle {
    float size = 16;
    int weight = 400;
    float letterSpacing = 0;  // px added after every character, as CSS does
    Align align = Align::Left;
    bool tnum = false;
};

// A node draws itself in its local space, after translation by (x, y) plus the
// x/y tracks, then rotation and scaling around (ox, oy).
struct Node {
    NodeKind kind = NodeKind::Group;
    float x = 0, y = 0;
    float w = 0, h = 0, radius = 0;          // Rect, Image; Glow uses w/h as ellipse radii; Circle: radius (or the value track)
    std::vector<float> pts;                  // Line, Polyline: x0 y0 x1 y1 ...
    std::shared_ptr<SkPath> path;            // Path
    sk_sp<SkImage> image;                    // Image, drawn into (0, 0, w, h)
    sk_sp<SkShader> shader;                  // Rect/Path fill pattern (e.g. hatch); used instead of fill
    Color fill, stroke;
    float strokeW = 0;
    bool roundCap = false;
    std::vector<float> dash;
    std::string text;
    TextStyle style;
    std::function<std::string(double)> format;  // set on counters: text = format(value at t)
    float ox = 0, oy = 0;
    bool clip = false;                       // clip children/self to (clipX, clipY, clipW(t), clipH)
    float clipX = 0, clipY = 0, clipW = 0, clipH = 0;

    Track<float> opacity, tx, ty, scale, scaleX, scaleY, rotation, value, clipWidth;
    Track<float> trim;                       // Line/Polyline/Path stroke drawn from 0 to trim (0..1)
    Track<Color> color;                      // overrides fill (or text colour) when present
    Track<Color> strokeColor;                // overrides stroke when present

    std::vector<std::unique_ptr<Node>> children;
    std::shared_ptr<Baked> baked;            // set by cache()
    sk_sp<SkTextBlob> blob;                  // static text shaped once by prepare()
    float blobWidth = 0;

    Node* add(std::unique_ptr<Node> n) { children.push_back(std::move(n)); return children.back().get(); }
};

std::unique_ptr<Node> group(float x = 0, float y = 0);
std::unique_ptr<Node> rect(float x, float y, float w, float h, Color fill, float radius = 0);
std::unique_ptr<Node> line(float x1, float y1, float x2, float y2, Color stroke, float width = 1);
std::unique_ptr<Node> text(float x, float y, std::string s, TextStyle st, Color fill);
// Path from SVG path data ("M0 0 L10 0 ...").
std::unique_ptr<Node> pathNode(const std::string& svgPath, Color fill, Color stroke = {}, float width = 0);
// Circle or arc as a stroked path starting at 12 o'clock, clockwise; `sweep` in degrees.
std::unique_ptr<Node> arc(float cx, float cy, float r, float startDeg, float sweepDeg, Color stroke, float width);
std::unique_ptr<Node> imageNode(float x, float y, float w, float h, sk_sp<SkImage> img);

// Diagonal stripes, as an SVG <pattern> rotated 45° with a solid background.
sk_sp<SkShader> hatchShader(Color background, Color stripe, float period = 8, float width = 3);

// Marks a subtree as static: it is rendered once at device resolution on first
// draw and blitted after that. The subtree and its ancestors must have no tracks.
Node* cache(Node* n);

// Renders a subtree once into an image of w x h stage units at the given pixel scale,
// with (ox, oy) mapped to the image's top-left. For static content that animates as a whole.
sk_sp<SkImage> rasterize(const Node& n, float ox, float oy, float w, float h, float pixelScale, const Fonts& fonts);

// Shapes every static text once, so frames only draw blobs. Call before rendering.
void prepare(Node& root, const Fonts& fonts);

void draw(SkCanvas* canvas, const Node& node, float t, const Fonts& fonts);
