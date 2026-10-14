#pragma once
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "render/graph.h"

struct Fonts;

struct Video {
    std::unique_ptr<Node> root;      // in stage units, 1280x720
    float duration = 0;
    std::vector<float> B;            // scene boundaries; scene k runs from B[k] to B[k+1]
    std::vector<std::string> warnings;
};

// Expands a plan into one scene graph with tracks for the whole video.
// baseDir resolves asset paths; pixelScale is output pixels per stage unit (for logo rasters).
Video buildVideo(const nlohmann::json& plan, const Fonts& fonts, const std::string& baseDir, float pixelScale);
