#include "render/image.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "modules/svg/include/SkSVGDOM.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open image: " + path);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static LoadedImage loadSvg(const std::string& path, int ph, const std::string& recolor) {
    std::string svg = readFile(path);
    if (!recolor.empty()) {
        svg = std::regex_replace(svg, std::regex(R"re(fill="(?!none)[^"]*")re"), "fill=\"" + recolor + "\"");
        svg = std::regex_replace(svg, std::regex(R"re(fill:\s*(?!none)[^;"']+)re"), "fill:" + recolor);
    }
    std::smatch m;
    if (!std::regex_search(svg, m, std::regex(R"re(viewBox="\s*([-\d.]+)[\s,]+([-\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+))re")))
        throw std::runtime_error("svg without viewBox: " + path);
    const float vw = std::stof(m[3]), vh = std::stof(m[4]);
    // Size comes from the viewBox and the container; drop the root's own width/height/style.
    const size_t open = svg.find("<svg");
    const size_t close = svg.find('>', open);
    std::string root = svg.substr(open, close - open);
    root = std::regex_replace(root, std::regex(R"re(\s(width|height|style)="[^"]*")re"), "");
    svg = svg.substr(0, open) + root + svg.substr(close);

    SkMemoryStream stream(svg.data(), svg.size());
    auto dom = SkSVGDOM::Builder().setFontManager(SkFontMgr::RefEmpty()).make(stream);
    if (!dom) throw std::runtime_error("cannot parse svg: " + path);
    const int pw = (int)std::ceil(ph * vw / vh);
    dom->setContainerSize(SkSize::Make((float)pw, (float)ph));
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pw, ph));
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    dom->render(surface->getCanvas());
    return {surface->makeImageSnapshot(), vw / vh};
}

static LoadedImage loadRaster(const std::string& path, int ph) {
    // Probe the size, then decode scaled to the target height as raw RGBA.
    const std::string probe = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \"" + path + "\"";
    FILE* p = popen(probe.c_str(), "r");
    int w = 0, h = 0;
    if (!p || std::fscanf(p, "%d,%d", &w, &h) != 2) {
        if (p) pclose(p);
        throw std::runtime_error("cannot read image: " + path);
    }
    pclose(p);
    const int pw = (int)std::lround((double)ph * w / h);
    std::string cmd = "ffmpeg -v error -i \"" + path + "\" -vf scale=" + std::to_string(pw) + ":" + std::to_string(ph) +
                      ":flags=lanczos -f rawvideo -pix_fmt rgba -";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
    p = popen(cmd.c_str(), "rb");
#else
    p = popen(cmd.c_str(), "r");
#endif
    std::vector<uint8_t> px((size_t)pw * ph * 4);
    const size_t got = p ? std::fread(px.data(), 1, px.size(), p) : 0;
    if (p) pclose(p);
    if (got != px.size()) throw std::runtime_error("cannot decode image: " + path);
    const SkImageInfo info = SkImageInfo::Make(pw, ph, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    auto img = SkImages::RasterFromData(info, SkData::MakeWithCopy(px.data(), px.size()), (size_t)pw * 4);
    return {img, (float)w / h};
}

LoadedImage loadImage(const std::string& path, int pixelHeight, const std::string& recolor) {
    const bool svg = path.size() > 4 && (path.substr(path.size() - 4) == ".svg" || path.substr(path.size() - 4) == ".SVG");
    return svg ? loadSvg(path, pixelHeight, recolor) : loadRaster(path, pixelHeight);
}
