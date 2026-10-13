#pragma once
#include <map>
#include <string>
#include <vector>

#include "include/core/SkRefCnt.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "render/graph.h"

struct hb_font_t;
class SkFontMgr;

// One variable font file, instanced at the weights we use.
struct Fonts {
    explicit Fonts(const std::string& path);
    ~Fonts();
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    sk_sp<SkTypeface> face(int weight) const;
    hb_font_t* hb(int weight) const;
    unsigned upem = 1000;

private:
    std::vector<char> data_;
    sk_sp<SkFontMgr> mgr_;
    std::map<int, sk_sp<SkTypeface>> faces_;
    std::map<int, hb_font_t*> hb_;
};

struct Shaped {
    sk_sp<SkTextBlob> blob;  // origin at the baseline start
    float width = 0;
};

Shaped shape(const Fonts& fonts, const std::string& utf8, const TextStyle& st);
float measure(const Fonts& fonts, const std::string& utf8, const TextStyle& st);

// Greedy line breaking on spaces, as a browser does for plain text.
std::vector<std::string> wrap(const Fonts& fonts, const std::string& utf8, const TextStyle& st, float maxWidth);
