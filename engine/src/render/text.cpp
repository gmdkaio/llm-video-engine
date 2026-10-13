#include "render/text.h"

#include <fstream>
#include <stdexcept>

#include <hb.h>

#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontArguments.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"

#if __has_include("include/ports/SkFontMgr_empty.h")
#include "include/ports/SkFontMgr_empty.h"
static sk_sp<SkFontMgr> makeMgr() { return SkFontMgr_New_Custom_Empty(); }
#else
#include "include/ports/SkTypeface_win.h"
static sk_sp<SkFontMgr> makeMgr() { return SkFontMgr_New_DirectWrite(); }
#endif

static const int kWeights[] = {400, 500, 600, 700};

Fonts::Fonts(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open font: " + path);
    data_.assign(std::istreambuf_iterator<char>(f), {});
    mgr_ = makeMgr();

    hb_blob_t* blob = hb_blob_create(data_.data(), (unsigned)data_.size(), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    upem = hb_face_get_upem(face);
    for (int w : kWeights) {
        SkFontArguments::VariationPosition::Coordinate coord{SkSetFourByteTag('w', 'g', 'h', 't'), (float)w};
        SkFontArguments args;
        args.setVariationDesignPosition({&coord, 1});
        faces_[w] = mgr_->makeFromStream(SkMemoryStream::MakeCopy(data_.data(), data_.size()), args);
        if (!faces_[w]) throw std::runtime_error("skia could not load font: " + path);

        hb_font_t* font = hb_font_create(face);
        hb_variation_t var{HB_TAG('w', 'g', 'h', 't'), (float)w};
        hb_font_set_variations(font, &var, 1);
        hb_font_set_scale(font, (int)upem, (int)upem);
        hb_font_make_immutable(font);
        hb_[w] = font;
    }
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

Fonts::~Fonts() {
    for (auto& [w, f] : hb_) hb_font_destroy(f);
}

static int nearest(int weight) {
    int best = kWeights[0];
    for (int w : kWeights)
        if (std::abs(w - weight) < std::abs(best - weight)) best = w;
    return best;
}

sk_sp<SkTypeface> Fonts::face(int weight) const { return faces_.at(nearest(weight)); }
hb_font_t* Fonts::hb(int weight) const { return hb_.at(nearest(weight)); }

Shaped shape(const Fonts& fonts, const std::string& utf8, const TextStyle& st) {
    Shaped out;
    if (utf8.empty()) return out;

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8.data(), (int)utf8.size(), 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_feature_t feats[1];
    unsigned nfeat = 0;
    if (st.tnum) feats[nfeat++] = {HB_TAG('t', 'n', 'u', 'm'), 1, 0, (unsigned)-1};
    hb_shape(fonts.hb(st.weight), buf, feats, nfeat);

    unsigned n = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    const float k = st.size / fonts.upem;

    SkFont font(fonts.face(st.weight), st.size);
    font.setSubpixel(true);
    font.setHinting(SkFontHinting::kNone);
    font.setEdging(SkFont::Edging::kAntiAlias);

    SkTextBlobBuilder builder;
    const auto& run = builder.allocRunPos(font, (int)n);
    float x = 0;
    for (unsigned i = 0; i < n; ++i) {
        run.glyphs[i] = (SkGlyphID)info[i].codepoint;
        run.points()[i] = {x + pos[i].x_offset * k, -pos[i].y_offset * k};
        x += pos[i].x_advance * k;
        // CSS letter-spacing goes after each character; a cluster boundary marks one.
        if (i + 1 == n || info[i + 1].cluster != info[i].cluster) x += st.letterSpacing;
    }
    hb_buffer_destroy(buf);
    out.blob = builder.make();
    out.width = x;
    return out;
}

float measure(const Fonts& fonts, const std::string& utf8, const TextStyle& st) { return shape(fonts, utf8, st).width; }

std::vector<std::string> wrap(const Fonts& fonts, const std::string& utf8, const TextStyle& st, float maxWidth) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&] {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(fonts, trial, st) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (char c : utf8) {
        if (c == ' ') flush(); else word += c;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}
