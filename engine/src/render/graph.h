#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/tracks.h"
#include "render/color.h"

class SkCanvas;
struct Fonts;
struct Baked;

enum class NodeKind { Group, Rect, Line, Polyline, Text, Glow };
enum class Align { Left, Center, Right };

struct TextStyle {
    float size = 16;
    int weight = 400;
    float letterSpacing = 0;  // px added after every character, as CSS does
    Align align = Align::Left;
    bool tnum = false;
};

// A node draws itself in its local space, after translation by (x, y) plus the
// x/y tracks, and scaling around (ox, oy).
struct Node {
    NodeKind kind = NodeKind::Group;
    float x = 0, y = 0;
    float w = 0, h = 0, radius = 0;          // Rect; Glow uses w/h as ellipse radii
    std::vector<float> pts;                  // Line, Polyline: x0 y0 x1 y1 ...
    Color fill, stroke;
    float strokeW = 0;
    std::vector<float> dash;
    std::string text;
    TextStyle style;
    std::function<std::string(double)> format;  // set on counters: text = format(value at t)
    float ox = 0, oy = 0;
    bool clip = false;                       // clip children/self to (clipX, clipY, clipW(t), clipH)
    float clipX = 0, clipY = 0, clipW = 0, clipH = 0;

    Track<float> opacity, tx, ty, scale, scaleX, scaleY, value, clipWidth;
    Track<Color> color;                      // overrides fill (or text colour) when present

    std::vector<std::unique_ptr<Node>> children;
    std::shared_ptr<Baked> baked;            // set by cache()


    Node* add(std::unique_ptr<Node> n) { children.push_back(std::move(n)); return children.back().get(); }
};

std::unique_ptr<Node> group(float x = 0, float y = 0);
std::unique_ptr<Node> rect(float x, float y, float w, float h, Color fill, float radius = 0);
std::unique_ptr<Node> line(float x1, float y1, float x2, float y2, Color stroke, float width = 1);
std::unique_ptr<Node> text(float x, float y, std::string s, TextStyle st, Color fill);

// Marks a subtree as static: it is rendered once at device resolution on first
// draw and blitted after that. The subtree and its ancestors must have no tracks.
Node* cache(Node* n);

void draw(SkCanvas* canvas, const Node& node, float t, const Fonts& fonts);
