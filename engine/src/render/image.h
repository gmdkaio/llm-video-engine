#pragma once
#include <string>

#include "include/core/SkRefCnt.h"

class SkImage;

struct LoadedImage {
    sk_sp<SkImage> image;
    float aspect = 1;  // width / height
};

// Loads a logo or slide capture as a raster image `pixelHeight` tall.
// SVG goes through Skia's SVG module; `recolor` (a hex color) replaces every
// non-none fill first, for a single-color logo on a dark background.
// PNG and JPEG are decoded by ffmpeg.
LoadedImage loadImage(const std::string& path, int pixelHeight, const std::string& recolor = "");
